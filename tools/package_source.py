"""Create a source-only upload archive; never include machine-local settings."""
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "output/algorithm-source.tar.gz"
FILES = ("CMakeLists.txt", ".env.example", ".gitignore", "DEPLOY.md")
DIRECTORIES = ("net", "redis", "sql", "src", "web", "assets", "tools", "deploy")


def include(path):
    return (path.is_file() and not path.is_symlink()
            and "__pycache__" not in path.parts
            and not any(part.startswith(".env") and part != ".env.example" for part in path.parts)
            and not path.name.endswith((".pyc", "Zone.Identifier", ".sql", ".pem", ".key")))


def main():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    paths = [ROOT / name for name in FILES]
    for directory in DIRECTORIES:
        paths.extend((ROOT / directory).rglob("*"))
    with tarfile.open(OUTPUT, "w:gz") as archive:
        for path in sorted(paths):
            if not include(path):
                continue
            info = archive.gettarinfo(str(path), "algorithm/" + str(path.relative_to(ROOT)))
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = 0o644
            with path.open("rb") as stream:
                archive.addfile(info, stream)
    with tarfile.open(OUTPUT) as archive:
        for member in archive.getmembers():
            assert "/build/" not in member.name and not member.name.endswith("/.env")
    print("Created " + str(OUTPUT) + " (source only; .env and build artifacts excluded)")


if __name__ == "__main__":
    main()
