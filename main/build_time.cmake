# Writes the local build time to OUT. Run on every build (see CMakeLists.txt) so the clock
# starts from a fresh time even when main.c did not change.
string(TIMESTAMP now "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUT}" "#define BUILD_TIME \"${now}\"\n")
