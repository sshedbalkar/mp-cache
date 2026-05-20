SHELL := /bin/sh

# Tool override. Set CMAKE=/path/to/cmake when testing a specific CMake build.
CMAKE ?= cmake
SCRIPT_CONFIG_FILE ?= configs/scripts/defaults.env
script_default = $(shell . "$(SCRIPT_CONFIG_FILE)"; printf '%s' "$$$(1)")

# Artifact metadata used by `make package`; empty VERSION uses the server config.
VERSION ?=
COMMIT ?= $(call script_default,MP_SCRIPT_DEFAULT_ARTIFACT_COMMIT)
BUILD_TIME ?= $(call script_default,MP_SCRIPT_DEFAULT_ARTIFACT_BUILD_TIME)

# Build and package locations. BUILD_DIR must contain mp-cache-server and
# mp-cachectl before packaging a deployable tarball.
LOCAL_BUILD_PRESET ?= $(call script_default,MP_SCRIPT_DEFAULT_LOCAL_BUILD_PRESET)
BUILD_DIR ?= $(call script_default,MP_SCRIPT_DEFAULT_LOCAL_BUILD_DIR)
PACKAGE_DIR ?= $(call script_default,MP_SCRIPT_DEFAULT_PACKAGE_DIR)
BUILD_ARTIFACT_DIR ?= $(call script_default,MP_SCRIPT_DEFAULT_LOCAL_PACKAGE_DIR)
SKIP_VERSION_INCREMENT ?= 0

# Validation output knobs. TEST_REPORT_DIR keeps generated reports under .tmp/
# by default, and STANDARDS_MIN is the minimum accepted standards score.
TEST_REPORT_DIR ?= $(call script_default,MP_SCRIPT_DEFAULT_TEST_REPORT_DIR)
STANDARDS_MIN ?= $(call script_default,MP_SCRIPT_DEFAULT_STANDARDS_MINIMUM_SCORE)

# Remote deployment inputs. ARTIFACT is required by deploy-* targets, and
# SSH_TARGET is required by remote deploy/stop/restart targets.
ARTIFACT ?=
SSH_TARGET ?=

.PHONY: build native-config native-build build-artifact test test-full test-unit test-naming-strategy test-standards test-hardening test-valgrind benchmark check package build-local test-local test-local-deployment deploy-local deploy-development deploy-qa deploy-staging deploy-production stop-local stop-development stop-qa stop-staging stop-production restart-local restart-development restart-qa restart-staging restart-production doctor promote clean

# Default build compiles the native service and creates a versioned local artifact
# using the build_version configured in configs/bootstrap.yaml.
build: build-artifact

# Regenerate build files from the checked-in CMake preset.
native-config:
	$(CMAKE) --fresh --preset "$(LOCAL_BUILD_PRESET)"

# Compile all configured local build targets.
native-build: native-config
	$(CMAKE) --build --preset "$(LOCAL_BUILD_PRESET)"

# Create the stable local build artifact consumed by deploy-local.sh.
build-artifact: native-build
	@if [ "$(SKIP_VERSION_INCREMENT)" = "1" ]; then \
		bash -c '. ./scripts/lib/version-env.sh; printf "build version: %s\n" "$$(mp_read_build_version_from_config "$$MP_CONFIG_PATH")"'; \
	else \
		bash -c '. ./scripts/lib/version-env.sh; printf "build version: %s\n" "$$(mp_increment_config_build_version "$$MP_CONFIG_PATH")"'; \
	fi
	./scripts/create-build-artifact.sh "$(BUILD_ARTIFACT_DIR)" "$(BUILD_DIR)" "$(COMMIT)" "$(BUILD_TIME)"

# Fast required validation set. Valgrind is intentionally separate because it
# depends on host memory tooling and debug-symbol setup.
test: test-unit test-naming-strategy test-standards test-hardening

# Full local validation, including the host-prepared Valgrind memory check.
test-full: test test-valgrind

# Unit tests rebuild first so test binaries match the current source tree.
test-unit: native-build
	./scripts/test-unit.sh "$(TEST_REPORT_DIR)"

# Naming strategy guards low-signal names in cmd/, scripts/, and internal/.
test-naming-strategy:
	./scripts/test-naming-strategy.sh "$(TEST_REPORT_DIR)"

# Standards check verifies durable deployment/docs/script inventory and drift.
test-standards:
	./scripts/check-standards.sh "$(TEST_REPORT_DIR)" "$(STANDARDS_MIN)"

# Hardening runs logging checks plus sanitizer-backed native validation.
test-hardening:
	./scripts/test-hardening.sh "$(TEST_REPORT_DIR)"

# Valgrind is the host memory check; use test-full when the host supports it.
test-valgrind:
	./scripts/test-valgrind.sh "$(TEST_REPORT_DIR)"

# Cache benchmark writes its report beside other validation artifacts.
benchmark: native-build
	./scripts/benchmark-cache.sh "$(TEST_REPORT_DIR)"

# Context check verifies the retrieval map and durable doc anchors exist.
check:
	./scripts/check-context.sh

# Package the selected BUILD_DIR into PACKAGE_DIR with release metadata.
package: native-build
	./scripts/package.sh "$(PACKAGE_DIR)" "$(BUILD_DIR)" "$(VERSION)" "$(COMMIT)" "$(BUILD_TIME)"

# Local development helpers keep the older rootless service flow available.
build-local:
	./scripts/build-local.sh

test-local:
	./scripts/test-local.sh

# Post-deployment smoke tests exercise the installed local systemd service
# through the local Nginx proxy and write reports under .tmp/deploy/local/.
test-local-deployment:
	./scripts/test-local-deployment.sh

# Local auto-start deployment installs a current-user systemd service behind
# local Nginx. It may prompt for administrator privileges.
deploy-local:
	./scripts/deploy-local.sh

# Remote deployment targets install an immutable package over SSH.
# Example: make deploy-qa ARTIFACT=dist/promotions/qa/mp-cache-v.tar.gz SSH_TARGET=deploy@qa-host
deploy-development:
	./scripts/deploy-development.sh "$(ARTIFACT)" "$(SSH_TARGET)"

deploy-qa:
	./scripts/deploy-qa.sh "$(ARTIFACT)" "$(SSH_TARGET)"

deploy-staging:
	./scripts/deploy-staging.sh "$(ARTIFACT)" "$(SSH_TARGET)"

deploy-production:
	./scripts/deploy-production.sh "$(ARTIFACT)" "$(SSH_TARGET)"

# Stop targets mirror the local lifecycle across all supported environments.
stop-local:
	./scripts/stop-local-server.sh

stop-development:
	./scripts/stop-development.sh "$(SSH_TARGET)"

stop-qa:
	./scripts/stop-qa.sh "$(SSH_TARGET)"

stop-staging:
	./scripts/stop-staging.sh "$(SSH_TARGET)"

stop-production:
	./scripts/stop-production.sh "$(SSH_TARGET)"

# Restart targets mirror the local lifecycle across all supported environments.
restart-local:
	./scripts/restart-local-server.sh

restart-development:
	./scripts/restart-development.sh "$(SSH_TARGET)"

restart-qa:
	./scripts/restart-qa.sh "$(SSH_TARGET)"

restart-staging:
	./scripts/restart-staging.sh "$(SSH_TARGET)"

restart-production:
	./scripts/restart-production.sh "$(SSH_TARGET)"

# Doctor reports the local environment, build, config, and running server state.
doctor:
	./scripts/doctor-local.sh

# Promote copies an existing artifact/checksum into the target promotion lane.
promote:
	./scripts/promote-artifact.sh

# Remove generated build, package, and default test-report outputs.
clean:
	rm -rf build dist .tmp/test-reports
