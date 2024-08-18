all:
	bash ./scripts/build_in_docker.sh src
	mkdir -p build
	cp src/build/client build
	cp src/build/compute_node build
	cp src/build/enclave.signed.so build
	cp src/build/lib/penglai/libpenglai_0.so build

clean:
	rm -rf build
	rm -rf src/build
