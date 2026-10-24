from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_webclient_resource_closure import active_resource_manifests, check


class ResourceClosureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.qml = self.root / "qml.qrc"
        self.resources = self.root / "resources.qrc"
        (self.root / "components").mkdir()
        (self.root / "third_party/fuse").mkdir(parents=True)
        (self.root / "pages").mkdir()
        (self.root / "icons").mkdir()
        (self.root / "components/Search.mjs").write_text(
            'import Fuse from "../third_party/fuse/fuse.mjs";\n'
            'export function create() { return new Fuse([]); }\n', encoding="utf-8")
        (self.root / "third_party/fuse/fuse.mjs").write_text(
            "export default class Fuse {}\n", encoding="utf-8")
        (self.root / "pages/About.qml").write_text(
            'import QtQuick\nimport "../components/Search.mjs" as Search\n'
            'Item { Image { source: "qrc:/icons/search.svg" } }\n', encoding="utf-8")
        (self.root / "icons/search.svg").write_text("<svg/>", encoding="utf-8")
        self.write_qml_manifest(include_fuse=True)
        self.write_resource_manifest(include_icon=True)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write_qml_manifest(self, include_fuse: bool) -> None:
        fuse = ('    <file alias="third_party/fuse/fuse.mjs">'
                f'{self.root}/third_party/fuse/fuse.mjs</file>\n') if include_fuse else ""
        self.qml.write_text(
            '<RCC><qresource prefix="/qt/qml/Decenza/">\n'
            f'    <file alias="components/Search.mjs">{self.root}/components/Search.mjs</file>\n'
            f'    <file alias="pages/About.qml">{self.root}/pages/About.qml</file>\n'
            f'{fuse}</qresource></RCC>\n', encoding="utf-8")

    def write_resource_manifest(self, include_icon: bool) -> None:
        icon = (f'    <file alias="search.svg">{self.root}/icons/search.svg</file>\n'
                if include_icon else "")
        self.resources.write_text(
            '<RCC><qresource prefix="/icons">\n' + icon + '</qresource></RCC>\n',
            encoding="utf-8")

    def test_transitive_relative_es_module_is_required(self) -> None:
        self.write_qml_manifest(include_fuse=False)
        errors = check(self.qml, [self.qml, self.resources])
        self.assertEqual(len(errors), 1)
        self.assertIn("third_party/fuse/fuse.mjs", errors[0])
        self.assertIn("components/Search.mjs", errors[0])

    def test_literal_qrc_resource_must_be_registered(self) -> None:
        self.write_resource_manifest(include_icon=False)
        errors = check(self.qml, [self.qml, self.resources])
        self.assertEqual(len(errors), 1)
        self.assertIn("qrc:/icons/search.svg", errors[0])
        self.assertIn("pages/About.qml", errors[0])

    def write_target_sources(self, *sources: Path) -> Path:
        path = self.root / "target_sources.txt"
        path.write_text(f"source-root={self.root}\nbuild-root={self.root}\n"
                        + "\n".join(str(source) for source in sources) + "\n", encoding="utf-8")
        return path

    def test_stale_manifest_cannot_supply_an_unlinked_resource(self) -> None:
        self.write_resource_manifest(include_icon=False)
        stale = self.root / "retired.qrc"
        stale.write_text('<RCC><qresource prefix="/icons">'
                         f'<file alias="search.svg">{self.root}/icons/search.svg</file>'
                         '</qresource></RCC>', encoding="utf-8")
        sources = self.write_target_sources(self.root / "qrc_qml.cpp", self.root / "qrc_resources.cpp")
        manifests = active_resource_manifests(self.root, sources)
        self.assertNotIn(stale.resolve(), manifests)
        errors = check(self.qml, manifests)
        self.assertEqual(len(errors), 1)
        self.assertIn("qrc:/icons/search.svg", errors[0])

    def test_absolute_and_relative_active_sources_normalize_to_the_same_manifests(self) -> None:
        sources = self.write_target_sources(self.root / "qrc_qml.cpp", Path("qrc_resources.cpp"))
        manifests = active_resource_manifests(self.root, sources)
        self.assertEqual(manifests, sorted([self.qml.resolve(), self.resources.resolve()]))
        self.assertEqual(check(self.qml, manifests), [])

    def test_missing_or_unsupported_active_manifests_fail_closed(self) -> None:
        for source in [self.root / "qrc_missing.cpp", self.root / "elsewhere/qrc_unknown.cpp"]:
            sources = self.write_target_sources(source)
            with self.assertRaises(ValueError):
                active_resource_manifests(self.root, sources)
        sources = self.write_target_sources(self.root / "ordinary.cpp")
        with self.assertRaises(ValueError):
            active_resource_manifests(self.root, sources)
        with self.assertRaises(OSError):
            active_resource_manifests(self.root, self.root / "absent_sources.txt")

    def test_malformed_target_source_manifest_fails_closed(self) -> None:
        sources = self.root / "target_sources.txt"
        for body in ["", "ordinary.cpp\n", "source-root=.\nbuild-root=.\nqrc_qml.cpp\n",
                     f"source-root={self.root}\nbuild-root={self.root}\n$<UNRESOLVED>\n"]:
            sources.write_text(body, encoding="utf-8")
            with self.assertRaises(ValueError):
                active_resource_manifests(self.root, sources)

    def test_explicit_classic_qrc_must_belong_to_the_target(self) -> None:
        classic = self.root / "classic/emoji.qrc"
        classic.parent.mkdir()
        classic.write_text('<RCC><qresource prefix="/emoji"/></RCC>', encoding="utf-8")
        sources = self.write_target_sources(self.root / "qrc_qml.cpp", self.root / "classic/qrc_emoji.cpp")
        self.assertIn(classic.resolve(), active_resource_manifests(self.root, sources, [classic]))
        sources = self.write_target_sources(self.root / "qrc_qml.cpp")
        with self.assertRaises(ValueError):
            active_resource_manifests(self.root, sources, [classic])

    def test_complete_closure_passes(self) -> None:
        self.assertEqual(check(self.qml, [self.qml, self.resources]), [])


if __name__ == "__main__":
    unittest.main()
