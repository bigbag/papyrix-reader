# Makefile for PapyriX Reader firmware
# Wraps PlatformIO commands for convenience

.PHONY: all build build-release release package upload upload-release flash flash-release flash-xteink-c3 flash-x4pro flash-x4c \
        clean format check monitor size erase build-fs upload-fs sleep-screen gh-release changelog help \
        test test-build test-run test-tools test-clean fontconvert-bin reader-test

# Prefer the project environment when it provides PlatformIO.
ifneq ($(wildcard $(CURDIR)/.venv/bin/pio),)
export PATH := $(CURDIR)/.venv/bin:$(PATH)
endif

# Default target
all: help

# Build targets
build: ## Build firmware (default environment)
	pio run

build-release: ## Build all release firmware environments
	pio run -e release_xteink_c3 -e release_x4pro -e release_x4c

release: build-release ## Alias for build-release

package: ## Build, check, and package all release firmware
	python3 scripts/package_firmware.py
	python3 test/scripts/test_sdmmc_mount.py

# Upload targets
upload: ## Build and flash to device
	pio run --target upload

flash-xteink-c3: ## Build and flash release firmware for X3/X4
	pio run -e release_xteink_c3 --target upload

flash-x4pro: ## Build and flash release firmware for X4 Pro (hold Power)
	pio run -e release_x4pro --target upload

flash-x4c: ## Build and flash release firmware for X4 Classic
	@port="$${PLATFORMIO_UPLOAD_PORT:-}"; \
	if [ -z "$$port" ]; then port=$$(python3 scripts/select_usb_jtag_port.py) || exit 1; fi; \
	pio run -e release_x4c --target upload --upload-port "$$port"

# Aliases
upload-release: flash-xteink-c3 ## Alias for flash-xteink-c3 (X3/X4)

flash: upload ## Alias for upload

flash-release: upload-release ## Alias for upload-release

# Clean
clean: ## Clean build artifacts
	pio run --target clean

# Code quality
format: ## Format code with clang-format
	./bin/clang-format-fix

check: ## Run static analysis (cppcheck)
	pio check

# Device/debug
monitor: ## Open serial monitor
	pio device monitor

size: ## Show firmware size
	pio run --target size

erase: ## Erase device flash
	pio run --target erase

# Filesystem
build-fs: ## Build filesystem image
	pio run --target buildfs

upload-fs: ## Upload filesystem to device
	pio run --target uploadfs

# Release
tag: ## Create and push a version tag (triggers GitHub release)
	@read -p "Enter tag version (e.g., 1.0.0): " TAG; \
	if [[ $$TAG =~ ^[0-9]+\.[0-9]+\.[0-9]+$$ ]]; then \
		git tag -a v$$TAG -m "v$$TAG"; \
		git push origin v$$TAG; \
		echo "Tag v$$TAG created and pushed successfully."; \
	else \
		echo "Invalid tag format. Please use X.Y.Z (e.g., 1.0.0)"; \
		exit 1; \
	fi

gh-release: package ## Create GitHub release with all firmware artifacts
ifndef VERSION
	$(error VERSION is required. Usage: make gh-release VERSION=0.1.1 [NOTES="..."])
endif
ifdef NOTES
	gh release create v$(VERSION) dist/papyrix-xteink-c3.bin dist/papyrix-x4pro.bin dist/papyrix-x4c.bin \
		dist/manifest.json \
		--repo bigbag/papyrix-reader \
		--title "PapyriX v$(VERSION)" \
		--notes "$(NOTES)"
else
	gh release create v$(VERSION) dist/papyrix-xteink-c3.bin dist/papyrix-x4pro.bin dist/papyrix-x4c.bin \
		dist/manifest.json \
		--repo bigbag/papyrix-reader \
		--title "PapyriX v$(VERSION)" \
		--generate-notes
endif

changelog: ## Generate CHANGELOG.md from git history
	@echo "Generating CHANGELOG.md..."
	@echo "" > CHANGELOG.md; \
	previous_tag=0; \
	for current_tag in $$(git tag --sort=-creatordate); do \
		if [ "$$previous_tag" != 0 ]; then \
			tag_date=$$(git log -1 --pretty=format:'%ad' --date=short $${previous_tag}); \
			printf "\n## $${previous_tag} ($${tag_date})\n\n" >> CHANGELOG.md; \
			git log $${current_tag}...$${previous_tag} --pretty=format:'*  %s [[%an](mailto:%ae)]' --reverse | grep -v Merge >> CHANGELOG.md; \
			printf "\n" >> CHANGELOG.md; \
		fi; \
		previous_tag=$${current_tag}; \
	done; \
	if [ "$$previous_tag" != 0 ]; then \
		tag_date=$$(git log -1 --pretty=format:'%ad' --date=short $${previous_tag}); \
		printf "\n## $${previous_tag} ($${tag_date})\n\n" >> CHANGELOG.md; \
		git log $${previous_tag} --pretty=format:'*  %s [[%an](mailto:%ae)]' --reverse | grep -v Merge >> CHANGELOG.md; \
		printf "\n" >> CHANGELOG.md; \
	fi
	@echo "CHANGELOG.md generated successfully."

# Image conversion
sleep-screen: ## Convert image to sleep screen BMP
ifdef INPUT
ifdef OUTPUT
	cd scripts && node create-sleep-screen.mjs ../$(INPUT) ../$(OUTPUT) $(ARGS)
else
	@echo "Usage: make sleep-screen INPUT=<image> OUTPUT=<bmp> [ARGS='--dither --bits 8']"
endif
else
	@echo "Usage: make sleep-screen INPUT=<image> OUTPUT=<bmp> [ARGS='--dither --bits 8']"
	@echo "Example: make sleep-screen INPUT=photo.jpg OUTPUT=sleep.bmp"
endif

## Unit Tests:

test: test-build test-run ## Build and run C/C++ unit tests

test-build: ## Build unit tests
	@mkdir -p test/build
	@cd test/build && cmake .. -DCMAKE_BUILD_TYPE=Debug && cmake --build . --parallel $(shell nproc | awk '{print ($$1 > 1 ? int($$1 / 2) : 1)}')

test-run: ## Run C/C++ unit tests (builds if needed)
	@if [ ! -d test/build/bin ]; then $(MAKE) test-build; fi
	@test/scripts/run_tests.sh

test-tools: ## Host-tool tests (packaging, HTML, clock simulators)
	@python3 test/scripts/LocaleExamplesTest.py
	@python3 test/scripts/WebUiFilenameValidationTest.py
	@python3 test/scripts/test_target_features.py
	@python3 test/scripts/test_sdmmc_lifecycle.py
	@python3 test/scripts/test_wakeup.py
	@python3 test/scripts/test_sleep_cancellation.py
	@python3 test/scripts/test_sleep_abort.py
	@python3 test/scripts/test_clock_app.py
	@python3 test/scripts/test_clock_faces.py
	@python3 test/scripts/test_clock_display.py
	@python3 test/scripts/test_package_firmware.py
	@python3 test/scripts/test_build_html.py
	@python3 scripts/test_select_usb_jtag_port.py

test-clean: ## Clean test build artifacts
	@rm -rf test/build

## Tools:

fontconvert-bin: ## Build Go fontconvert-bin tool (CJK .bin font converter)
	$(MAKE) -C tools/fontconvert-bin build

reader-test: ## Build desktop reader-test tool (process books without flashing)
	@mkdir -p tools/reader-test/build
	@cd tools/reader-test/build && cmake .. && cmake --build . --parallel $(shell nproc | awk '{print ($$1 > 1 ? int($$1 / 2) : 1)}')
	@echo "Built: tools/reader-test/build/reader-test"
ifdef FILE
	@tools/reader-test/build/reader-test $(FILE) $(OUTPUT)
endif

## Help:

help: ## Show this help
	@echo "PapyriX Reader - Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@awk 'BEGIN {FS = ":.*##"; section=""} \
		/^##/ { section=substr($$0, 4); next } \
		/^[a-zA-Z0-9_-]+:.*##/ { \
			if (section != "") { printf "\n\033[1m%s\033[0m\n", section; section="" } \
			printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2 \
		}' $(MAKEFILE_LIST)
	@echo ""