SHELL := /bin/sh

CMAKE ?= cmake
VERSION ?= dev
COMMIT ?= local
BUILD_TIME ?= 1970-01-01T00:00:00Z
BUILD_DIR ?= build/local-debug
PACKAGE_DIR ?= dist
TEST_REPORT_DIR ?= .tmp/test-reports
STANDARDS_MIN ?= 85

.PHONY: build native-config native-build test test-unit test-standards check package build-local test-local deploy-local stop-local restart-local doctor promote clean

build: native-build

native-config:
	$(CMAKE) --fresh --preset local-debug

native-build: native-config
	$(CMAKE) --build --preset local-debug

test: test-unit test-standards

test-unit: native-build
	./scripts/test-unit.sh "$(TEST_REPORT_DIR)"

test-standards:
	./scripts/check-standards.sh "$(TEST_REPORT_DIR)" "$(STANDARDS_MIN)"

check:
	./scripts/check-context.sh

package: build
	./scripts/package.sh "$(PACKAGE_DIR)" "$(BUILD_DIR)" "$(VERSION)" "$(COMMIT)" "$(BUILD_TIME)"

build-local:
	./scripts/build-local.sh

test-local:
	./scripts/test-local.sh

deploy-local:
	./scripts/deploy-local.sh

stop-local:
	./scripts/stop-local-server.sh

restart-local:
	./scripts/restart-local-server.sh

doctor:
	./scripts/doctor-local.sh

promote:
	./scripts/promote-artifact.sh

clean:
	rm -rf build dist .tmp/test-reports
