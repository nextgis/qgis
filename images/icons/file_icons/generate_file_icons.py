#!/usr/bin/env python3

import argparse
import html
import os
import re
import shutil
import subprocess
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple, cast

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def bootstrap_local_venv() -> None:
    script_path = Path(__file__).resolve()
    repository_root = script_path.parents[3]
    venv_python = repository_root / ".venv" / "bin" / "python"
    if (
        "VIRTUAL_ENV" not in os.environ
        and venv_python.exists()
        and Path(sys.prefix).resolve() != (repository_root / ".venv").resolve()
    ):
        os.environ["VIRTUAL_ENV"] = str(repository_root / ".venv")
        os.execv(str(venv_python), [str(venv_python), str(script_path), *sys.argv[1:]])


bootstrap_local_venv()

from PyQt5.QtGui import (
    QFont,
    QFontMetricsF,
    QPainterPath,
)
from PyQt5.QtWidgets import QApplication
from svgelements import Matrix
import yaml


LABEL_PADDING = 9.0
LABEL_FONT_SIZE_PT = 30.0
LABEL_TEXT_COLOR = "#ffffff"
EXT_BG_EDGE_PADDING = 0.1
SVG_WIDTH = 256
SVG_HEIGHT = 256
SVG_OUTPUT_DIRECTORY = Path("svg")
MAC_OUTPUT_DIRECTORY = Path("mac")
ICNS_CHUNKS = (
    (16, "icp4"),
    (32, "icp5"),
    (64, "icp6"),
    (128, "ic07"),
    (256, "ic08"),
    (512, "ic09"),
    (1024, "ic10"),
)
MOVE_TO_ELEMENT = 0
LINE_TO_ELEMENT = 1
CURVE_TO_ELEMENT = 2
SVG_NAMESPACE = "http://www.w3.org/2000/svg"
XLINK_NAMESPACE = "http://www.w3.org/1999/xlink"
Rect = Tuple[float, float, float, float]
_qt_application = None


ET.register_namespace("", SVG_NAMESPACE)
ET.register_namespace("xlink", XLINK_NAMESPACE)


@dataclass(frozen=True)
class SvgRect:
    x: float
    y: float
    width: float
    height: float


@dataclass(frozen=True)
class IconSpec:
    name: str
    extension: str
    component: Path
    file_extensions: Tuple[str, ...]
    compressed: bool = False

    @classmethod
    def from_mapping(cls, value: object) -> "IconSpec":
        if not isinstance(value, dict):
            raise ValueError("Icon entry must be a mapping")

        file_extensions = value.get("file_extensions", [])
        if not isinstance(file_extensions, list):
            raise ValueError("Icon file_extensions must be a list")

        return cls(
            name=str(value["name"]),
            extension=str(value["extension"]),
            component=Path(str(value["component"])),
            file_extensions=tuple(str(extension) for extension in file_extensions),
            compressed=bool(value.get("compressed")),
        )


@dataclass(frozen=True)
class ManifestConfig:
    path: Path
    base_directory: Path
    default_background: Path
    compression_icon: Optional[Path]
    icons: Tuple[IconSpec, ...]

    @classmethod
    def load(cls, manifest_path: Path) -> "ManifestConfig":
        raw_manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(raw_manifest, dict):
            raise ValueError("Manifest root must be a mapping")

        raw_icons = raw_manifest.get("icons")
        if not isinstance(raw_icons, list):
            raise ValueError("Manifest must contain an icons list")

        compression_icon = raw_manifest.get("compression_icon")
        return cls(
            path=manifest_path,
            base_directory=manifest_path.parent,
            default_background=Path(str(raw_manifest["default_background"])),
            compression_icon=(
                Path(str(compression_icon)) if compression_icon is not None else None
            ),
            icons=tuple(IconSpec.from_mapping(icon) for icon in raw_icons),
        )

    def selected_icons(
        self, selected_names: Optional[Iterable[str]]
    ) -> Tuple[IconSpec, ...]:
        if not selected_names:
            return self.icons

        selected = set(selected_names)
        return tuple(icon for icon in self.icons if icon.name in selected)

    def resolve(self, relative_path: Path) -> Path:
        return self.base_directory / relative_path


@dataclass(frozen=True)
class CompressionAsset:
    body: str
    viewbox: Tuple[float, float, float, float]

    @classmethod
    def empty(cls) -> "CompressionAsset":
        return cls("", (0.0, 0.0, 0.0, 0.0))


@dataclass(frozen=True)
class BackgroundRender:
    body: str
    extension_markup: str
    extension_rect: SvgRect


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate file format SVG icons from YAML manifest"
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("icons.yaml"),
        help="Path to the icon manifest",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=None,
        help="Root directory for generated icons",
    )
    parser.add_argument(
        "--icon",
        action="append",
        dest="icons",
        default=None,
        help="Generate only the named icon(s)",
    )
    return parser.parse_args()


class InkscapeRunner:
    def __init__(self) -> None:
        binary = shutil.which("inkscape")
        if binary is None:
            raise RuntimeError("inkscape was not found in PATH")
        self.binary = binary

    def run(self, arguments: List[str], input_path: Path) -> str:
        processing = subprocess.run(
            [self.binary, str(input_path), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
        if processing.returncode != 0:
            raise RuntimeError(
                f"inkscape failed for {input_path}: {processing.stdout.strip()}"
            )
        return processing.stdout

    def export_png(self, input_path: Path, output_path: Path, size: int) -> None:
        self.run(
            [
                "--export-type=png",
                f"--export-width={size}",
                f"--export-height={size}",
                f"--export-filename={output_path}",
            ],
            input_path,
        )
        if not output_path.exists():
            raise RuntimeError(
                f"inkscape did not create expected PNG output {output_path}"
            )


def prefix_svg_ids(svg_root: ET.Element, id_prefix: str = "") -> None:
    id_map: Dict[str, str] = {}
    for element in svg_root.iter():
        element_id = element.attrib.get("id")
        if element_id:
            id_map[element_id] = (
                f"{id_prefix}_{element_id}" if id_prefix else element_id
            )

    if not id_map:
        return

    url_pattern = re.compile(r"url\(#([^)]+)\)")

    for element in svg_root.iter():
        element_id = element.attrib.get("id")
        if element_id and id_prefix:
            element.set("id", id_map[element_id])

        for attribute_name, attribute_value in list(element.attrib.items()):
            if attribute_name == "id":
                continue

            is_href = attribute_name == "href" or attribute_name.endswith("}href")
            if is_href and attribute_value.startswith("#"):
                referenced_id = attribute_value[1:]
                if referenced_id in id_map:
                    element.set(attribute_name, f"#{id_map[referenced_id]}")
                else:
                    del element.attrib[attribute_name]
                continue

            if attribute_name == "style":
                style_rules = []
                for style_rule in attribute_value.split(";"):
                    if not style_rule.strip():
                        continue
                    referenced_ids = url_pattern.findall(style_rule)
                    has_unknown_reference = any(
                        referenced_id not in id_map for referenced_id in referenced_ids
                    )
                    if has_unknown_reference:
                        continue
                    style_rules.append(
                        url_pattern.sub(
                            lambda match: f"url(#{id_map[match.group(1)]})",
                            style_rule,
                        )
                    )
                updated_value = ";".join(style_rules)
                if updated_value:
                    element.set(attribute_name, updated_value)
                else:
                    del element.attrib[attribute_name]
                continue

            referenced_ids = url_pattern.findall(attribute_value)
            if any(referenced_id not in id_map for referenced_id in referenced_ids):
                del element.attrib[attribute_name]
                continue

            def replace_url(match: re.Match) -> str:
                referenced_id = match.group(1)
                return f"url(#{id_map.get(referenced_id, referenced_id)})"

            updated_value = url_pattern.sub(replace_url, attribute_value)
            if updated_value != attribute_value:
                element.set(attribute_name, updated_value)


def extract_svg_viewbox(svg_path: Path) -> Tuple[float, float, float, float]:
    svg_root = ET.parse(svg_path).getroot()
    view_box = svg_root.attrib.get("viewBox")
    if view_box:
        min_x, min_y, width, height = [float(value) for value in view_box.split()]
        return min_x, min_y, width, height

    width = float(svg_root.attrib["width"])
    height = float(svg_root.attrib["height"])
    return 0.0, 0.0, width, height


def find_element_with_transform(
    element: ET.Element,
    target_id: str,
    inherited_transform: Optional[Matrix] = None,
) -> Tuple[Optional[ET.Element], Matrix]:
    if inherited_transform is None:
        inherited_transform = Matrix()
    element_transform = Matrix(element.attrib.get("transform", ""))
    current_transform = element_transform * inherited_transform

    if element.attrib.get("id") == target_id:
        return element, current_transform

    for child in list(element):
        found_element, found_transform = find_element_with_transform(
            child, target_id, current_transform
        )
        if found_element is not None:
            return found_element, found_transform

    return None, current_transform


def find_parent_of_element(
    root: ET.Element, target_element: ET.Element
) -> Optional[ET.Element]:
    for child in list(root):
        if child is target_element:
            return root
        parent = find_parent_of_element(child, target_element)
        if parent is not None:
            return parent
    return None


def serialize_svg_body(svg_root: ET.Element) -> str:
    return "\n".join(
        ET.tostring(child, encoding="unicode") for child in list(svg_root)
    )


def parse_rect_geometry(element: ET.Element) -> Tuple[float, float, float, float]:
    rect_attributes = ("x", "y", "width", "height")
    if all(attribute_name in element.attrib for attribute_name in rect_attributes):
        return (
            float(element.attrib["x"]),
            float(element.attrib["y"]),
            float(element.attrib["width"]),
            float(element.attrib["height"]),
        )

    path_data = element.attrib.get("d", "")
    rectangle_path = re.fullmatch(
        r"\s*[mM]\s*([-+]?\d*\.?\d+),?\s*([-+]?\d*\.?\d+)"
        r"\s*[hH]\s*([-+]?\d*\.?\d+)"
        r"\s*[vV]\s*([-+]?\d*\.?\d+)"
        r"\s*[hH]\s*([-+]?\d*\.?\d+)\s*[zZ]\s*",
        path_data,
    )
    if rectangle_path is None:
        raise ValueError(
            "Unsupported ext_bg geometry: expected rect attributes or rectangle "
            f"path, got {path_data!r}"
        )

    x = float(rectangle_path.group(1))
    y = float(rectangle_path.group(2))
    width = float(rectangle_path.group(3))
    height = float(rectangle_path.group(4))
    return x, y, abs(width), abs(height)


def get_style_property(element: ET.Element, property_name: str) -> Optional[str]:
    for style_rule in element.attrib.get("style", "").split(";"):
        if ":" not in style_rule:
            continue
        name, value = style_rule.split(":", 1)
        if name.strip() == property_name:
            return value.strip()
    return None


def parse_svg_number(value: Optional[str]) -> Optional[float]:
    if value is None:
        return None

    number_match = re.match(
        r"\s*([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)", value
    )
    if number_match is None:
        return None
    return float(number_match.group(1))


def get_svg_numeric_property(
    element: ET.Element, property_name: str, default: float = 0.0
) -> float:
    property_value = element.attrib.get(property_name)
    if property_value is None:
        property_value = get_style_property(element, property_name)

    parsed_value = parse_svg_number(property_value)
    if parsed_value is None:
        return default
    return parsed_value


def get_stroke_outset_x(element: ET.Element, scale_x: float) -> float:
    stroke_width = get_svg_numeric_property(element, "stroke-width")
    vector_effect = element.attrib.get("vector-effect")
    if vector_effect is None:
        vector_effect = get_style_property(element, "vector-effect")

    if vector_effect == "non-scaling-stroke":
        return stroke_width / 2.0
    return stroke_width * scale_x / 2.0


def update_rect_geometry(element: ET.Element, x: float, width: float) -> None:
    if "x" in element.attrib and "width" in element.attrib:
        element.set("x", f"{x:.5f}")
        element.set("width", f"{width:.5f}")
        return

    _, y, _, height = parse_rect_geometry(element)
    element.set(
        "d",
        f"m {x:.5f},{y:.5f} h {width:.5f} v {height:.5f} H {x:.5f} Z",
    )


def ensure_qt_application() -> QApplication:
    global _qt_application
    application = QApplication.instance()
    if application is not None:
        return cast(QApplication, application)
    if _qt_application is None:
        _qt_application = QApplication([])
    return _qt_application


class SvgAssetExtractor:
    def body(self, svg_path: Path, id_prefix: Optional[str] = None) -> str:
        svg_root = ET.parse(svg_path).getroot()
        prefix_svg_ids(svg_root, id_prefix or "")
        return serialize_svg_body(svg_root).strip()

    def viewbox(self, svg_path: Path) -> Tuple[float, float, float, float]:
        return extract_svg_viewbox(svg_path)


class BackgroundBuilder:
    def __init__(self, background_path: Path) -> None:
        self.background_path = background_path

    def build(self, label_width: float) -> BackgroundRender:
        background_tree = ET.parse(self.background_path)
        background_root = background_tree.getroot()

        extension_rect, rect_transform = find_element_with_transform(
            background_root, "ext_bg"
        )
        shadow_rect, _ = find_element_with_transform(
            background_root, "ext_bg_shadow"
        )
        if extension_rect is None or shadow_rect is None:
            raise ValueError("document.svg must contain ext_bg and ext_bg_shadow")

        rect_x, rect_y, rect_width, rect_height = parse_rect_geometry(extension_rect)
        scale_x = abs(rect_transform.a)
        scale_y = abs(rect_transform.d)
        if scale_x == 0.0 or scale_y == 0.0:
            raise ValueError("ext_bg transform must have non-zero scale")

        required_width = max(rect_width, (label_width + LABEL_PADDING * 2) / scale_x)
        extra_width = required_width - rect_width
        rect_left, _ = rect_transform.point_in_matrix_space((rect_x, rect_y))
        stroke_outset_x = max(
            get_stroke_outset_x(extension_rect, scale_x),
            get_stroke_outset_x(shadow_rect, scale_x),
        )
        available_left_space = max(
            rect_left - stroke_outset_x - EXT_BG_EDGE_PADDING, 0.0
        ) / scale_x
        shift_left = min(extra_width, available_left_space)
        new_x = rect_x - shift_left
        new_width = rect_width + extra_width

        for element in (extension_rect, shadow_rect):
            update_rect_geometry(element, new_x, new_width)

        rect_global_x, rect_global_y = rect_transform.point_in_matrix_space(
            (new_x, rect_y)
        )
        rect_geometry = SvgRect(
            x=rect_global_x,
            y=rect_global_y,
            width=new_width * scale_x,
            height=rect_height * scale_y,
        )
        ext_group, _ = find_element_with_transform(background_root, "ext")
        prefix_svg_ids(background_root, "background")
        ext_markup = ""
        if ext_group is not None:
            ext_parent = find_parent_of_element(background_root, ext_group)
            if ext_parent is not None:
                ext_parent.remove(ext_group)
                ext_markup = ET.tostring(ext_group, encoding="unicode")
        return BackgroundRender(
            body=serialize_svg_body(background_root),
            extension_markup=ext_markup,
            extension_rect=rect_geometry,
        )


class LabelBuilder:
    def __init__(self) -> None:
        ensure_qt_application()
        self.font = QFont("sans-serif")
        self.font.setStyleHint(QFont.SansSerif)
        self.font.setPointSizeF(LABEL_FONT_SIZE_PT)
        self.font.setBold(True)
        self.metrics = QFontMetricsF(self.font)

    def path(self, label_text: str) -> QPainterPath:
        label_path = QPainterPath()
        label_path.addText(0, 0, self.font, label_text.upper())
        return label_path

    def markup(
        self, label_text: str, label_path: QPainterPath, extension_rect: SvgRect
    ) -> str:
        label_bounds = label_path.boundingRect()
        translate_x = (
            extension_rect.x
            + (extension_rect.width - label_bounds.width()) / 2.0
            - label_bounds.x()
        )
        cap_path = QPainterPath()
        cap_path.addText(0, 0, self.font, "H")
        cap_bounds = cap_path.boundingRect()
        translate_y = extension_rect.y + extension_rect.height / 2.0
        translate_y -= (cap_bounds.top() + cap_bounds.bottom()) / 2.0
        label_elements = ['  <g id="extension_label" data-role="extension-label">']
        for character_index, character_path in enumerate(
            self.character_paths(label_text)
        ):
            translated_character_path = character_path.translated(
                translate_x, translate_y
            )
            character_path_data = self.path_to_svg_data(translated_character_path)
            label_elements.append(
                f"    <path d=\"{character_path_data}\" "
                f"fill=\"{LABEL_TEXT_COLOR}\" fill-rule=\"nonzero\" "
                f"data-glyph=\"{character_index}\" />"
            )
        label_elements.append("  </g>")
        return "\n".join(label_elements)

    def character_paths(self, label_text: str) -> List[QPainterPath]:
        paths = []
        cursor_x = 0.0
        for character in label_text.upper():
            character_path = QPainterPath()
            character_path.addText(cursor_x, 0, self.font, character)
            paths.append(character_path)
            cursor_x += self.metrics.horizontalAdvance(character)
        return paths

    def path_to_svg_data(self, painter_path: QPainterPath) -> str:
        commands: List[str] = []
        element_index = 0
        while element_index < painter_path.elementCount():
            path_element = painter_path.elementAt(element_index)
            element_type = int(path_element.type)
            if element_type == MOVE_TO_ELEMENT:
                commands.append(f"M{path_element.x:.3f},{path_element.y:.3f}")
                element_index += 1
                continue
            if element_type == LINE_TO_ELEMENT:
                commands.append(f"L{path_element.x:.3f},{path_element.y:.3f}")
                element_index += 1
                continue
            if element_type == CURVE_TO_ELEMENT:
                control_point_one = path_element
                control_point_two = painter_path.elementAt(element_index + 1)
                end_point = painter_path.elementAt(element_index + 2)
                commands.append(
                    (
                        f"C{control_point_one.x:.3f},{control_point_one.y:.3f} "
                        f"{control_point_two.x:.3f},{control_point_two.y:.3f} "
                        f"{end_point.x:.3f},{end_point.y:.3f}"
                    )
                )
                element_index += 3
                continue
            element_index += 1
        return " ".join(commands)


class IconRenderer:
    @staticmethod
    def compression_markup(
        compression_body: str,
        compression_viewbox: Tuple[float, float, float, float],
        extension_rect: SvgRect,
    ) -> str:
        min_x, min_y, width, _height = compression_viewbox
        translate_x = extension_rect.x + extension_rect.width - (min_x + width / 2.0)
        translate_y = extension_rect.y - min_y
        return "".join(
            [
                f"  <g id=\"compression_icon\" "
                f"transform=\"translate({translate_x:.3f},{translate_y:.3f})\">\n",
                f"{compression_body}\n",
                "  </g>",
            ]
        )

    def render(
        self,
        background: BackgroundRender,
        label_markup: str,
        overlay_body: str,
        compression_markup: str,
        icon_name: str,
        manifest_path: Path,
    ) -> str:
        generated_from = manifest_path.as_posix()
        return "\n".join(
            [
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
                (
                    "<svg width=\"256\" height=\"256\" viewBox=\"0 0 256 256\" "
                    "version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" "
                    "xmlns:xlink=\"http://www.w3.org/1999/xlink\">"
                ),
                "  <!-- Generated by file_icons/generate_file_icons.py "
                f"from {generated_from}. -->",
                f"  <!-- Icon: {html.escape(icon_name)} -->",
                background.body,
                overlay_body,
                background.extension_markup,
                compression_markup,
                label_markup,
                "</svg>",
                "",
            ]
        )


class SvgOptimizer:
    def __init__(self) -> None:
        binary = shutil.which("svgo")
        if binary is None:
            raise RuntimeError("svgo was not found in PATH")
        self.binary = binary

    def optimize(self, svg_path: Path) -> None:
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".mjs", encoding="utf-8"
        ) as config_file:
            config_file.write(
                "export default {\n"
                "  plugins: [\n"
                "    {\n"
                "      name: 'preset-default',\n"
                "      params: { overrides: { cleanupIds: false } },\n"
                "    },\n"
                "  ],\n"
                "};\n"
            )
            config_file.flush()
            optimization = subprocess.run(
                [
                    self.binary,
                    "--config",
                    config_file.name,
                    "--multipass",
                    str(svg_path),
                    "-o",
                    str(svg_path),
                ],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                universal_newlines=True,
            )
        if optimization.returncode != 0:
            raise RuntimeError(
                f"svgo failed for {svg_path}: {optimization.stdout.strip()}"
            )


class IcnsBuilder:
    def __init__(self, inkscape: InkscapeRunner) -> None:
        self.inkscape = inkscape

    def build(self, svg_path: Path, icns_path: Path) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            chunks = []
            for size, chunk_type in ICNS_CHUNKS:
                png_path = temporary_path / f"{svg_path.stem}_{size}.png"
                self.inkscape.export_png(svg_path, png_path, size)
                png_data = png_path.read_bytes()
                chunks.append(
                    chunk_type.encode("ascii")
                    + struct.pack(">I", len(png_data) + 8)
                    + png_data
                )

        icns_data = b"".join(chunks)
        icns_path.parent.mkdir(parents=True, exist_ok=True)
        icns_path.write_bytes(
            b"icns" + struct.pack(">I", len(icns_data) + 8) + icns_data
        )


class FileIconGenerator:
    def __init__(
        self,
        manifest: ManifestConfig,
        output_root: Path,
        selected_names: Optional[Iterable[str]],
    ) -> None:
        self.manifest = manifest
        self.output_root = output_root
        self.selected_names = selected_names
        self.inkscape = InkscapeRunner()
        self.assets = SvgAssetExtractor()
        self.background_builder = BackgroundBuilder(
            manifest.resolve(manifest.default_background)
        )
        self.label_builder = LabelBuilder()
        self.renderer = IconRenderer()
        self.optimizer = SvgOptimizer()
        self.icns_builder = IcnsBuilder(self.inkscape)

    def load_compression_asset(self) -> CompressionAsset:
        if self.manifest.compression_icon is None:
            return CompressionAsset.empty()

        compression_path = self.manifest.resolve(self.manifest.compression_icon)
        return CompressionAsset(
            body=self.assets.body(compression_path, "compression"),
            viewbox=self.assets.viewbox(compression_path),
        )

    def compression_markup(
        self,
        icon: IconSpec,
        compression: CompressionAsset,
        extension_rect: SvgRect,
    ) -> str:
        if not icon.compressed:
            return ""
        return self.renderer.compression_markup(
            compression.body,
            compression.viewbox,
            extension_rect,
        )

    def output_path(self, relative_path: Path) -> Path:
        output_path = (self.output_root / relative_path).resolve()
        if self.output_root not in output_path.parents and output_path != self.output_root:
            raise ValueError(
                f"Output path {output_path} is outside of output root "
                f"{self.output_root}"
            )
        return output_path

    def icon_stem(self, icon: IconSpec) -> str:
        return icon.extension.lower()

    def svg_output_path(self, icon: IconSpec) -> Path:
        return self.output_path(SVG_OUTPUT_DIRECTORY / f"{self.icon_stem(icon)}.svg")

    def icns_output_path(self, icon: IconSpec) -> Path:
        return self.output_path(MAC_OUTPUT_DIRECTORY / f"{self.icon_stem(icon)}.icns")

    def generate_icon(
        self, icon: IconSpec, compression: CompressionAsset
    ) -> Tuple[Path, Path]:
        component_path = self.manifest.resolve(icon.component)
        overlay_body = self.assets.body(component_path, "component")
        label_path = self.label_builder.path(icon.extension)
        background = self.background_builder.build(
            label_path.boundingRect().width()
        )
        label_markup = self.label_builder.markup(
            icon.extension, label_path, background.extension_rect
        )
        compression_markup = self.compression_markup(
            icon, compression, background.extension_rect
        )
        svg_text = self.renderer.render(
            background=background,
            label_markup=label_markup,
            overlay_body=overlay_body,
            compression_markup=compression_markup,
            icon_name=icon.name,
            manifest_path=self.manifest.path.relative_to(
                self.manifest.base_directory.parent
            ),
        )

        svg_path = self.svg_output_path(icon)
        svg_path.parent.mkdir(parents=True, exist_ok=True)
        svg_path.write_text(svg_text, encoding="utf-8")
        self.optimizer.optimize(svg_path)

        icns_path = self.icns_output_path(icon)
        self.icns_builder.build(svg_path, icns_path)
        return svg_path, icns_path

    def run(self) -> None:
        compression = self.load_compression_asset()
        for icon in self.manifest.selected_icons(self.selected_names):
            svg_path, icns_path = self.generate_icon(icon, compression)
            print(svg_path.relative_to(self.output_root))
            print(icns_path.relative_to(self.output_root))


def main() -> int:
    arguments = parse_args()
    manifest_path = arguments.manifest.resolve()
    output_root = (
        arguments.output_root.resolve()
        if arguments.output_root
        else manifest_path.parent.resolve()
    )
    generator = FileIconGenerator(
        manifest=ManifestConfig.load(manifest_path),
        output_root=output_root,
        selected_names=arguments.icons,
    )
    generator.run()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
