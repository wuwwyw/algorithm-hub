"""Extract the upload archive elsewhere and check a fresh build and startup."""
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    subprocess.run(["python3", str(ROOT / "tools/package_source.py")], check=True)
    clean_env = {key: value for key, value in os.environ.items() if not key.startswith("ALGORITHM_")}
    with tempfile.TemporaryDirectory(prefix="algorithm-deploy-") as directory:
        destination = Path(directory)
        with tarfile.open(ROOT / "output/algorithm-source.tar.gz") as archive:
            for member in archive.getmembers():
                resolved = (destination / member.name).resolve()
                assert destination.resolve() in resolved.parents and member.isfile()
                assert not member.name.endswith("/.env") and "/build/" not in member.name
            archive.extractall(destination)
        source = destination / "algorithm"
        assert not (source / ".env").exists()
        command = ["cmake", "-S", str(source), "-B", str(source / "build"), "-DCMAKE_BUILD_TYPE=Release"]
        # This WSL's ICU development headers are unpacked locally; cloud Ubuntu uses libicu-dev.
        local_headers = ROOT / "build/icu-dev/usr/include"
        if local_headers.exists():
            command.append("-DICU_INCLUDE_DIR=" + str(local_headers))
            original_cache = (ROOT / "build/CMakeCache.txt").read_text().splitlines()
            for component in ("UC", "I18N"):
                key = f"ICU_{component}_LIBRARY_RELEASE"
                value = next(line.split("=", 1)[1] for line in original_cache
                             if line.startswith(key + ":FILEPATH="))
                command.append("-D" + key + "=" + value)
        subprocess.run(command, check=True, env=clean_env)
        subprocess.run(["cmake", "--build", str(source / "build"), "-j2"], check=True, env=clean_env)
        executable = source / "build/algorithm_hub"
        missing = subprocess.run([str(executable), "--check-config"], capture_output=True, text=True, env=clean_env)
        assert missing.returncode == 1 and "ALGORITHM_DB_PASSWORD" in missing.stderr
        defaults = subprocess.run([str(executable), "--check-config"], capture_output=True, text=True,
                                  env=dict(clean_env, ALGORITHM_DB_PASSWORD="synthetic-config-check-only"))
        assert defaults.returncode == 0 and "user=algorithm_app" in defaults.stdout
        assert "synthetic-config-check-only" not in defaults.stdout
        # Read the original private file at runtime without copying it into the upload archive.
        test_env = dict(clean_env, ALGORITHM_CONFIG_FILE=str(ROOT / ".env"))
        subprocess.run(["python3", str(source / "tools/test_multi_epoll.py")], check=True, env=test_env)
        cache = (source / "build/CMakeCache.txt").read_text()
        for name, file in (("REGULAR", "LXGWWenKai-Regular.ttf"), ("BOLD", "LXGWWenKai-Medium.ttf"),
                           ("MONO", "LXGWWenKaiMono-Regular.ttf")):
            assert f"ALGORITHM_HUB_PDF_{name}_FONT_PATH:FILEPATH={source}/assets/fonts/{file}" in cache
        print(json.dumps(dict(source_archive_excludes_private_config=True, fresh_directory_build=True,
                              cloud_defaults=True, missing_password_fails_cleanly=True,
                              relocated_web_and_font_paths=True, relocated_network_regression=True)))


if __name__ == "__main__":
    main()
