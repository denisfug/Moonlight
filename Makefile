# Convenience wrapper around CMake. Real build logic lives in CMakeLists.txt.
#   make test   - PC build + all tests        make shots - PC screenshots into out/shots
#   make ps4    - PS4 ELF (needs OO_PS4_TOOLCHAIN, toolchain/libcxx; GOLDHEN_SDK optional)
#   make pkg    - PS4 fake PKG into out/
.PHONY: host test live shots libcxx ps4 pkg clean

host:
	cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build -j

test: host
	ML_FIXTURES=$(CURDIR)/tests/fixtures ./build/moonlight_tests

live: host
	tools/run_live_test.sh

shots: host
	mkdir -p out/shots/sb
	./build/moonlight_host out/shots/sb assets out/shots "shot:01_inicio left down shot:02_musica down shot:03_filmes right x shot:04_detalhe o left down right x down down shot:05_serie"

libcxx:
	toolchain/build-libcxx.sh

ps4:
	cmake -B build-ps4 -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
	cmake --build build-ps4 -j

pkg: ps4
	tools/make_pkg.sh build-ps4/moonlight out

clean:
	rm -rf build build-ps4 out
