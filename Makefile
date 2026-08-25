SHELL := /bin/bash
TOOLS := ./tools/scripts/tools

PRESET ?= mac-check
TARGET ?= m0
CLUSTER ?= rangpur
MODE ?= serial
THREADS ?= 1
RANKS ?= 1
NODES ?= 1
GPUS ?= 0
CONSTRAINT ?= unset
ACTION ?= run
ARGS ?=

export HPC_MAKE_ACTION := $(value ACTION)
export HPC_MAKE_ARGS := $(value ARGS)
export HPC_MAKE_CLUSTER := $(value CLUSTER)
export HPC_MAKE_CONSTRAINT := $(value CONSTRAINT)
export HPC_MAKE_GPUS := $(value GPUS)
export HPC_MAKE_MODE := $(value MODE)
export HPC_MAKE_NODES := $(value NODES)
export HPC_MAKE_PRESET := $(value PRESET)
export HPC_MAKE_PRESET_ORIGIN := $(origin PRESET)
export HPC_MAKE_RANKS := $(value RANKS)
export HPC_MAKE_TARGET := $(value TARGET)
export HPC_MAKE_TARGET_ORIGIN := $(origin TARGET)
export HPC_MAKE_THREADS := $(value THREADS)

POSITIONAL_COMMANDS := bench pgo check cov explore profile
POSITIONAL_COMMANDS += hpc package clean fmt san
POSITIONAL_COMMAND := $(firstword $(MAKECMDGOALS))
POSITIONAL_ARGUMENTS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
POSITIONAL_ARGUMENT_TARGETS := $(filter-out \
	$(POSITIONAL_COMMAND),$(POSITIONAL_ARGUMENTS))

export HPC_MAKE_COMMAND := $(POSITIONAL_COMMAND)
export HPC_MAKE_ARGUMENT_COUNT := $(words $(POSITIONAL_ARGUMENTS))
export HPC_MAKE_ARGUMENT_1 := $(word 1,$(POSITIONAL_ARGUMENTS))

ifneq ($(filter $(POSITIONAL_COMMAND),$(POSITIONAL_COMMANDS)),)

.PHONY: $(POSITIONAL_COMMAND) $(POSITIONAL_ARGUMENT_TARGETS)

ifneq ($(strip $(POSITIONAL_ARGUMENT_TARGETS)),)
$(POSITIONAL_ARGUMENT_TARGETS):
	@:
endif

$(POSITIONAL_COMMAND):
	$(TOOLS)/make

else

.PHONY: help configure build m0 m1 m2 a1 run test lint analyze \
	topology binary exegesis perf callgrind hpctoolkit scorep scalasca papi \
	report bolt submit package-a1 codeql

help:
	@bold= reset=; \
	if test -t 1; then \
		bold=$$(printf '\033[1m'); \
		reset=$$(printf '\033[0m'); \
	fi; \
	printf '%s\n' \
		"$${bold}=== COSC3500 Commands ===$${reset}" \
		'' \
		"$${bold}Build$${reset}" \
		'  make configure PRESET=mac-check' \
		'      Configure one CMake preset.' \
		'  make build TARGET=m0 PRESET=mac-check' \
		'      Build one program.' \
		'  make run TARGET=m0 PRESET=mac-check ARGS="..."' \
		'      Build and run one program.' \
		'  make test PRESET=mac-check' \
		'      Build everything, then run CTest.' \
		'' \
		"$${bold}Checks$${reset}" \
		'  make fmt | make -- fmt --fix' \
		'      Check or apply formatting.' \
		'  make check [lint|analyze|test|all]' \
		'      Run one check group; default: all.' \
		'  make codeql PRESET=bunya-check' \
		'      Build the C++ inputs used by CodeQL.' \
		'  make san au|t|m' \
		'      Run ASan+UBSan, TSan, or MSan.' \
		'  make cov [llvm|gcov|clean|report]' \
		'      Build or inspect one coverage lane.' \
		'' \
		"$${bold}Performance$${reset}" \
		'  make bench self-test|gates TARGET=m1' \
		'  make bench smoke|standard|publication TARGET=m1 PRESET=profile' \
		'  make pgo gen|use TARGET=m1' \
		'  make report TARGET=m1 PRESET=profile' \
		'  make profile perf-stat|perf-record|callgrind|...' \
		'  make explore all|topology|binary|assembly|...' \
		'  make bolt TARGET=m2' \
		'' \
		"$${bold}Cluster and files$${reset}" \
		'  make hpc doctor|nodes|queue|... CLUSTER=rangpur' \
		'  make submit CLUSTER=rangpur TARGET=m0 MODE=serial' \
		'  make package a1' \
		'  make package m1' \
		'  make clean PRESET=mac-check | make clean all' \
		'' \
		"$${bold}Defaults$${reset}" \
		'  PRESET=mac-check TARGET=m0 CLUSTER=rangpur MODE=serial' \
		'  THREADS=1 RANKS=1 NODES=1 GPUS=0 CONSTRAINT=unset'

configure:
	@. tools/scripts/tools/lib.sh; require_preset "$$HPC_MAKE_PRESET"
	cmake --preset "$$HPC_MAKE_PRESET"

build: configure
	@. tools/scripts/tools/lib.sh; require_target "$$HPC_MAKE_TARGET"
	cmake --build --preset "$$HPC_MAKE_PRESET" --target "$$HPC_MAKE_TARGET"

m0 m1 m2 a1:
	+$(MAKE) build TARGET=$@

run: build
	@args=(); \
	if test -n "$$HPC_MAKE_ARGS"; then \
		read -r -a args <<<"$$HPC_MAKE_ARGS"; \
	fi; \
		"build/$$HPC_MAKE_PRESET/bin/$$HPC_MAKE_TARGET" "$${args[@]}"

test: configure
	cmake --build --preset "$$HPC_MAKE_PRESET"
	ctest --test-dir "build/$$HPC_MAKE_PRESET" --output-on-failure

lint analyze:
	+$(MAKE) check $@ PRESET="$$HPC_MAKE_PRESET"

topology binary exegesis:
	+$(MAKE) explore $@ PRESET="$$HPC_MAKE_PRESET" \
		TARGET="$$HPC_MAKE_TARGET"

perf:
	+$(MAKE) profile perf-stat PRESET="$$HPC_MAKE_PRESET" \
		TARGET="$$HPC_MAKE_TARGET"

callgrind hpctoolkit scorep scalasca papi:
	+$(MAKE) profile $@ PRESET="$$HPC_MAKE_PRESET" \
		TARGET="$$HPC_MAKE_TARGET"

report: configure
	@. tools/scripts/tools/lib.sh; require_target "$$HPC_MAKE_TARGET"
	cmake --build --preset "$$HPC_MAKE_PRESET" \
		--target "$$HPC_MAKE_TARGET-optimisation-report"

bolt:
	$(TOOLS)/bench bolt --target "$$HPC_MAKE_TARGET"

submit:
	$(TOOLS)/hpc submit --cluster "$$HPC_MAKE_CLUSTER" \
		--action "$$HPC_MAKE_ACTION" --target "$$HPC_MAKE_TARGET" \
		--preset "$$HPC_MAKE_PRESET" --mode "$$HPC_MAKE_MODE" \
		--threads "$$HPC_MAKE_THREADS" --ranks "$$HPC_MAKE_RANKS" \
		--nodes "$$HPC_MAKE_NODES" --gpus "$$HPC_MAKE_GPUS" \
		--constraint "$$HPC_MAKE_CONSTRAINT"

package-a1:
	+$(MAKE) package a1

codeql: configure
	cmake --build --preset "$$HPC_MAKE_PRESET" --target m0 m1 hpc_bench

endif
