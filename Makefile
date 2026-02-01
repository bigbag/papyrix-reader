# Makefile for Papyrix Reader firmware
# Wraps PlatformIO commands for convenience

.PHONY: all build build-release release upload upload-release flash flash-release \
        clean format check monitor size erase build-fs upload-fs sleep-screen gh-release changelog help \
        test test-build test-run test-clean

# Default target
all: help

# Build targets
build: ## Build firmware (default environment)
	pio run

build-release: ## Build release firmware
	pio run -e gh_release

release: build-release ## Alias for build-release

# Upload targets
upload: ## Build and flash to device
	pio run --target upload

upload-release: ## Build and flash release firmware
	pio run -e gh_release --target upload

# Aliases
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

gh-release: build-release ## Create GitHub release with firmware
ifndef VERSION
	$(error VERSION is required. Usage: make gh-release VERSION=0.1.1 [NOTES="..."])
endif
ifdef NOTES
	gh release create v$(VERSION) .pio/build/gh_release/firmware.bin \
		--repo bigbag/papyrix-reader \
		--title "Papyrix v$(VERSION)" \
		--notes "$(NOTES)"
else
	gh release create v$(VERSION) .pio/build/gh_release/firmware.bin \
		--repo bigbag/papyrix-reader \
		--title "Papyrix v$(VERSION)" \
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

test: test-build test-run ## Build and run all unit tests

test-build: ## Build unit tests
	@mkdir -p test/build
	@cd test/build && cmake .. -DCMAKE_BUILD_TYPE=Debug && cmake --build . --parallel

test-run: ## Run unit tests (build first if needed)
	@if [ ! -d test/build/bin ]; then $(MAKE) test-build; fi
	@test/scripts/run_tests.sh

test-clean: ## Clean test build artifacts
	@rm -rf test/build

## Help:

help: ## Show this help
	@echo "Papyrix Reader - Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@awk 'BEGIN {FS = ":.*##"; section=""} \
		/^##/ { section=substr($$0, 4); next } \
		/^[a-zA-Z_-]+:.*##/ { \
			if (section != "") { printf "\n\033[1m%s\033[0m\n", section; section="" } \
			printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2 \
		}' $(MAKEFILE_LIST)
	@echo ""