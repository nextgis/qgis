"""QGIS Unit tests for per-request User-Agent control in QgsNetworkAccessManager.

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

__author__ = "notguiltyspark"
__date__ = "15/02/2026"
__copyright__ = "Copyright 2026, The QGIS Project"

import http.server
import socketserver
import threading

from qgis.PyQt.QtCore import QUrl
from qgis.PyQt.QtNetwork import QNetworkReply, QNetworkRequest
from qgis.PyQt.QtTest import QSignalSpy
from qgis.core import (
    Qgis,
    QgsNetworkAccessManager,
    QgsNetworkRequestParameters,
    QgsSettings,
)
import unittest
from qgis.testing import start_app, QgisTestCase

start_app()

# Fixed user-agent prefix for deterministic tests
FIXED_PREFIX = "TestBrowser/1.0"


class EchoHeaderHandler(http.server.BaseHTTPRequestHandler):
    """HTTP handler that echoes back the User-Agent header in the response body."""

    def do_GET(self):
        ua = self.headers.get("User-Agent", "")
        body = ua.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass  # suppress server log output during tests


class TestQgsNetworkUserAgent(QgisTestCase):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.httpd = socketserver.TCPServer(("localhost", 0), EchoHeaderHandler)
        cls.port = cls.httpd.server_address[1]
        cls.httpd_thread = threading.Thread(target=cls.httpd.serve_forever)
        cls.httpd_thread.daemon = True
        cls.httpd_thread.start()

        # Set a fixed user-agent prefix so tests are deterministic
        cls.settings = QgsSettings()
        cls.original_ua = cls.settings.value("/qgis/networkAndProxy/userAgent", "Mozilla/5.0")
        cls.settings.setValue("/qgis/networkAndProxy/userAgent", FIXED_PREFIX)

    @classmethod
    def tearDownClass(cls):
        cls.settings.setValue("/qgis/networkAndProxy/userAgent", cls.original_ua)
        cls.httpd.shutdown()
        cls.httpd.server_close()
        super().tearDownClass()

    def _fetch_user_agent(self, request):
        """Helper: send a GET request and return the echoed User-Agent string."""
        reply = QgsNetworkAccessManager.instance().get(request)
        spy = QSignalSpy(reply.finished)
        spy.wait(5000)
        self.assertEqual(reply.error(), QNetworkReply.NetworkError.NoError)
        return bytes(reply.readAll()).decode("utf-8")

    def _base_url(self):
        return f"http://localhost:{self.port}/"

    def _default_ua(self):
        return f"{FIXED_PREFIX} QGIS/{Qgis.versionInt()}/{__import__('platform').platform()}"

    def test_default_user_agent(self):
        """Test that the default User-Agent is correctly constructed."""
        request = QNetworkRequest(QUrl(self._base_url()))
        ua = self._fetch_user_agent(request)
        self.assertTrue(ua.startswith(FIXED_PREFIX))
        self.assertIn(f"QGIS/{Qgis.versionInt()}", ua)

    def test_user_agent_suffix(self):
        """Test that AttributeUserAgentSuffix appends to the default User-Agent."""
        request = QNetworkRequest(QUrl(self._base_url()))
        attr = QNetworkRequest.Attribute(QgsNetworkRequestParameters.RequestAttributes.AttributeUserAgentSuffix)
        request.setAttribute(attr, "MyPlugin/2.0")
        ua = self._fetch_user_agent(request)
        self.assertTrue(ua.startswith(FIXED_PREFIX))
        self.assertIn(f"QGIS/{Qgis.versionInt()}", ua)
        self.assertTrue(ua.endswith("MyPlugin/2.0"))

    def test_user_agent_override(self):
        """Test that AttributeUserAgentOverride completely replaces the User-Agent."""
        request = QNetworkRequest(QUrl(self._base_url()))
        attr = QNetworkRequest.Attribute(QgsNetworkRequestParameters.RequestAttributes.AttributeUserAgentOverride)
        request.setAttribute(attr, "CompletelyCustomAgent/3.0")
        ua = self._fetch_user_agent(request)
        self.assertEqual(ua, "CompletelyCustomAgent/3.0")

    def test_override_ignores_suffix(self):
        """Test that override takes precedence over suffix."""
        request = QNetworkRequest(QUrl(self._base_url()))
        suffix_attr = QNetworkRequest.Attribute(QgsNetworkRequestParameters.RequestAttributes.AttributeUserAgentSuffix)
        override_attr = QNetworkRequest.Attribute(QgsNetworkRequestParameters.RequestAttributes.AttributeUserAgentOverride)
        request.setAttribute(suffix_attr, "Suf/2.0")
        request.setAttribute(override_attr, "OverrideAgent/1.0")
        ua = self._fetch_user_agent(request)
        self.assertEqual(ua, "OverrideAgent/1.0")

    def test_preprocessor_can_modify_user_agent(self):
        """Test that a request preprocessor can still modify the User-Agent after our logic."""
        custom_ua = "PreprocessorAgent/1.0"

        def _preprocessor(request):
            request.setRawHeader(b"User-Agent", custom_ua.encode("utf-8"))

        pid = QgsNetworkAccessManager.setRequestPreprocessor(_preprocessor)
        try:
            request = QNetworkRequest(QUrl(self._base_url()))
            ua = self._fetch_user_agent(request)
            self.assertEqual(ua, custom_ua)
        finally:
            QgsNetworkAccessManager.removeRequestPreprocessor(pid)


if __name__ == "__main__":
    unittest.main()
