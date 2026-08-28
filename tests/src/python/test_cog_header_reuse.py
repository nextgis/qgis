"""QGIS Unit tests for COG header cache reuse with /vsis3/.

Tests that GDAL's VSIS3 cache reuses the COG header when opening the same
raster layer multiple times, both via GDAL and QGIS APIs.
"""

import gc
import re
import threading
from contextlib import contextmanager
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Type, Callable
from urllib.parse import urlsplit

import unittest
from osgeo import gdal
from qgis.core import QgsRasterLayer
from qgis.testing import start_app, QgisTestCase

from utilities import unitTestDataPath

start_app()

# ---- helpers from original script ----

_RANGE_PATTERN = re.compile(r"bytes=(\d+)-(\d*)")


@dataclass(frozen=True)
class _HttpRequest:
    method: str
    path: str
    range_header: Optional[str]


class _RequestRecorder:
    def __init__(self) -> None:
        self._requests: List[_HttpRequest] = []
        self._lock = threading.Lock()

    def append(self, request: _HttpRequest) -> None:
        with self._lock:
            self._requests.append(request)

    def snapshot(self) -> List[_HttpRequest]:
        with self._lock:
            return list(self._requests)

    def clear(self) -> None:
        with self._lock:
            self._requests.clear()


@dataclass(frozen=True)
class _MockS3:
    endpoint: str
    object_path: str
    recorder: _RequestRecorder


def _create_request_handler(
    object_path: str,
    object_data: bytes,
    recorder: _RequestRecorder,
) -> Type[BaseHTTPRequestHandler]:
    class ObjectRequestHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_HEAD(self) -> None:
            self._serve_object(include_body=False)

        def do_GET(self) -> None:
            self._serve_object(include_body=True)

        def log_message(self, format_string: str, *args: object) -> None:
            # Suppress standard HTTP server logging during tests.
            return

        def _serve_object(self, include_body: bool) -> None:
            request_path = urlsplit(self.path).path
            range_header = self.headers.get("Range")

            recorder.append(
                _HttpRequest(
                    method=self.command,
                    path=request_path,
                    range_header=range_header,
                )
            )

            if request_path != object_path:
                self.send_error(404)
                return

            file_size = len(object_data)
            start_offset = 0
            end_offset = file_size - 1
            status_code = 200

            if range_header is not None:
                match = _RANGE_PATTERN.fullmatch(range_header)
                if match is None:
                    self._send_range_error(file_size)
                    return

                start_offset = int(match.group(1))
                if match.group(2):
                    end_offset = int(match.group(2))

                end_offset = min(end_offset, file_size - 1)

                if start_offset >= file_size or start_offset > end_offset:
                    self._send_range_error(file_size)
                    return

                status_code = 206

            response_data = object_data[start_offset : end_offset + 1]

            self.send_response(status_code)
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Type", "image/tiff")
            self.send_header("Content-Length", str(len(response_data)))
            self.send_header("ETag", '"test-cog-etag"')
            self.send_header(
                "Last-Modified",
                "Wed, 01 Jan 2025 00:00:00 GMT",
            )

            if status_code == 206:
                self.send_header(
                    "Content-Range",
                    (f"bytes {start_offset}-{end_offset}/{file_size}"),
                )

            self.send_header("Connection", "close")
            self.end_headers()

            if include_body:
                self.wfile.write(response_data)

        def _send_range_error(self, file_size: int) -> None:
            self.send_response(416)
            self.send_header(
                "Content-Range",
                f"bytes */{file_size}",
            )
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()

    return ObjectRequestHandler


@contextmanager
def _gdal_config(
    options: Dict[str, str],
) -> Iterator[None]:
    previous_values = {option_name: gdal.GetConfigOption(option_name) for option_name in options}

    try:
        for option_name, option_value in options.items():
            gdal.SetConfigOption(option_name, option_value)

        yield
    finally:
        for option_name, option_value in previous_values.items():
            gdal.SetConfigOption(option_name, option_value)


def _open_and_release_layer_gdal(path: str) -> None:
    layer = gdal.OpenEx(path, gdal.OF_RASTER)
    assert layer is not None
    layer.GetGeoTransform()
    layer.GetProjection()
    layer.GetMetadata()
    layer.Close()

    del layer
    gc.collect()


def _open_and_release_layer_qgis(path: str) -> None:
    layer = QgsRasterLayer(path)
    assert layer.isValid()

    del layer
    gc.collect()


def _header_requests(
    requests: List[_HttpRequest],
    object_path: str,
) -> List[_HttpRequest]:
    return [
        request
        for request in requests
        if request.method == "GET"
        and request.path == object_path
        and (request.range_header is None or request.range_header.startswith("bytes=0-"))
    ]


def _format_requests(requests: List[_HttpRequest]) -> str:
    return "\n".join(
        (f"{request.method} {request.path} Range={request.range_header!r}") for request in requests
    )


# ---- test class ----

class TestVsiS3CacheReuse(QgisTestCase):

    @classmethod
    def setUpClass(cls):
        """Start the mock S3 server and set GDAL config options."""
        # GDAL options required for /vsis3/ and caching behavior
        cls._gdal_options = {
            "AWS_S3_ENDPOINT": "127.0.0.1:0",  # will be overridden after server starts
            "AWS_HTTPS": "NO",
            "AWS_VIRTUAL_HOSTING": "FALSE",
            "AWS_NO_SIGN_REQUEST": "YES",
            "GDAL_DISABLE_READDIR_ON_OPEN": "EMPTY_DIR",
            "GDAL_HTTP_MERGE_CONSECUTIVE_RANGES": "YES",
            "GDAL_PAM_ENABLED": "NO",
            "GDAL_INGESTED_BYTES_AT_OPEN": "32768",
            "CPL_VSIL_CURL_ALLOWED_EXTENSIONS": ".tif,.TIF,.tiff",
            "CPL_VSIL_CURL_CACHE_SIZE": "200000000",
            #"CPL_DEBUG": "ON",
        }
        # Store previous values for restore
        cls._prev_gdal_opts = {}
        for k, v in cls._gdal_options.items():
            cls._prev_gdal_opts[k] = gdal.GetConfigOption(k)
            gdal.SetConfigOption(k, v)

        # Set up mock server
        cog_path = Path(unitTestDataPath()) / "landsat.tif"
        
        object_data = b""
        with open(cog_path, 'rb') as f:
            object_data = f.read()

        cls._object_path = "/test-bucket/cache_test.tif"
        cls._recorder = _RequestRecorder()
        handler_class = _create_request_handler(
            object_path=cls._object_path,
            object_data=object_data,
            recorder=cls._recorder,
        )

        cls._server = ThreadingHTTPServer(
            ("127.0.0.1", 0),
            handler_class,
        )
        cls._server_thread = threading.Thread(
            target=cls._server.serve_forever,
            daemon=True,
        )
        cls._server_thread.start()

        port = cls._server.server_address[1]
        # Update endpoint in GDAL options (already set, but we need correct endpoint)
        # We'll override the option directly for the tests
        cls._endpoint = f"127.0.0.1:{port}"
        gdal.SetConfigOption("AWS_S3_ENDPOINT", cls._endpoint)

        # Build the vsis3 path
        cls._vsi_path = "/vsis3/test-bucket/cache_test.tif"

    @classmethod
    def tearDownClass(cls):
        """Shutdown server and restore GDAL options."""
        cls._server.shutdown()
        cls._server.server_close()
        cls._server_thread.join()

        # Restore previous GDAL options
        for k, v in cls._prev_gdal_opts.items():
            gdal.SetConfigOption(k, v)

    def setUp(self):
        """Clear request recorder before each test."""
        self._recorder.clear()
        gdal.VSICurlClearCache()

    def _run_reuse_test(self, opener1: Callable[[str], None], opener2: Callable[[str], None]):
        """
        Core test logic:
          - Open layer with opener1, assert header request occurred.
          - Open layer with opener2, assert no new header request (cache reuse).
          - Clear cache, open with opener1 again, assert header request occurs (cache miss).
        """
        # First open
        gdal.VSICurlClearCache()
        self._recorder.clear()
        opener1(self._vsi_path)

        requests_after_first = self._recorder.snapshot()
        first_header_requests = _header_requests(requests_after_first, self._object_path)
        self.assertTrue(
            first_header_requests,
            f"First opening did not request the COG header.\n{_format_requests(requests_after_first)}"
        )

        # Second open (should reuse cache)
        before_second = len(self._recorder.snapshot())
        opener2(self._vsi_path)

        requests_after_second = self._recorder.snapshot()
        second_open_requests = requests_after_second[before_second:]
        repeated_header_requests = _header_requests(second_open_requests, self._object_path)
        self.assertFalse(
            repeated_header_requests,
            f"Second opening downloaded the COG header again.\n{_format_requests(second_open_requests)}"
        )

        # Third open after clearing cache (should miss and fetch header)
        gdal.VSICurlClearCache()
        before_third = len(self._recorder.snapshot())
        opener1(self._vsi_path)

        third_open_requests = self._recorder.snapshot()[before_third:]
        header_after_clear = _header_requests(third_open_requests, self._object_path)
        self.assertTrue(
            header_after_clear,
            f"Opening after VSICurlClearCache() did not fetch the header.\n{_format_requests(third_open_requests)}"
        )

    def test_gdal_gdal(self):
        """Open twice via GDAL."""
        self._run_reuse_test(_open_and_release_layer_gdal, _open_and_release_layer_gdal)

    def test_qgis_qgis(self):
        """Open twice via QGIS."""
        self._run_reuse_test(_open_and_release_layer_qgis, _open_and_release_layer_qgis)

    def test_gdal_qgis(self):
        """Open via GDAL then QGIS."""
        self._run_reuse_test(_open_and_release_layer_gdal, _open_and_release_layer_qgis)

    def test_qgis_gdal(self):
        """Open via QGIS then GDAL."""
        self._run_reuse_test(_open_and_release_layer_qgis, _open_and_release_layer_gdal)


if __name__ == "__main__":
    unittest.main()
