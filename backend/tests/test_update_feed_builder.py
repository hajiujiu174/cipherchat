import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("feed_builder", ROOT / "scripts/build-update-feed.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class FeedBuilderTests(unittest.TestCase):
    # 验证bothproductspreserve全部文件字节andhashes。
    def test_both_products_preserve_all_file_bytes_and_hashes(self):
        work = ROOT / "work/feed-builder-tests"
        work.mkdir(parents=True, exist_ok=True)
        for product, executable in [("nextalk", "nextalk.exe"), ("messager", "Messager.exe")]:
            with self.subTest(product=product), tempfile.TemporaryDirectory(dir=work) as temporary:
                root = Path(temporary)
                package, feed = root / "package", root / "feed"
                package.mkdir()
                for name in [executable, "nextalk-updater.exe", "server-cert.pem", "update-config.json"]:
                    (package / name).write_bytes((name + ":fixture").encode())
                (package / "payload.bin").write_bytes(bytes(range(256)) * 1200)
                (package / "nextalk-launcher.exe").write_bytes(b"root-launcher-excluded")
                with contextlib.redirect_stdout(io.StringIO()):
                    builder.build(package, feed, "1.0", 1, "fixture", product=product)
                manifest = json.loads((package / "release.json").read_text())
                self.assertEqual(manifest, json.loads((feed / "latest.json").read_text()))
                self.assertEqual(manifest["product"], product)
                self.assertNotIn("nextalk-launcher.exe", {item["path"] for item in manifest["files"]})
                for item in manifest["files"]:
                    blocks = [(feed / "blobs" / block["sha256"]).read_bytes() for block in item["blocks"]]
                    self.assertEqual(b"".join(blocks), (package / item["path"]).read_bytes())
                    for data, block in zip(blocks, item["blocks"]):
                        self.assertEqual(hashlib.sha256(data).hexdigest(), block["sha256"])
                with self.assertRaises(ValueError), contextlib.redirect_stdout(io.StringIO()):
                    builder.build(package, feed, "1.1", 1, "changed", product=product)


if __name__ == "__main__":
    unittest.main()
