Import("env")

import os


def _ensure_gcc_on_path() -> None:
    if os.name != "nt":
        return

    platform = env.PioPlatform()
    toolchain_dir = platform.get_package_dir("toolchain-gccmingw32")
    if not toolchain_dir:
        return

    env.PrependENVPath("PATH", os.path.join(toolchain_dir, "bin"))


_ensure_gcc_on_path()

