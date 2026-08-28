PRESET ?= dev
export PRESET
SLURM_TARGET ?= m1
SAN ?= au
VALGRIND ?= memcheck
PERF_SCENE ?= templates/conway/1m
PERF_EVENTS ?= cycles,instructions,branches,branch-misses,cache-references,cache-misses
CLEAN_ALL := $(filter all,$(MAKECMDGOALS))

# Keep user-facing targets thin so CI and local runs share the scripts
.PHONY: m lint security valgrind fmt typecheck check cov package \
	profile record clean all slurm help

help:
	@printf '%s\n' \
		'make m | fmt | lint | typecheck | check | cov' \
		'make security SAN=m|t|l|au | valgrind VALGRIND=memcheck|cachegrind|callgrind|massif' \
		'make profile PRESET=cluster | package | clean [all] | slurm SLURM_TARGET=m0|m1'

m:
	cmake --preset $(PRESET)
	cmake --build --preset $(PRESET) --target m1

security:
	tools/scripts/security san $(SAN) --preset $(PRESET)

valgrind:
	tools/scripts/security valgrind $(VALGRIND) --preset $(PRESET)

lint:
	tools/scripts/check lint

fmt:
	tools/scripts/check fmt

typecheck:
	uv run --locked --offline basedpyright

check: fmt lint
	tools/scripts/test.sh test

cov:
	tools/scripts/check cov

package:
	tools/scripts/package

profile: m
	# Count hardware events outside the timed simulation process
	perf stat -r 5 -e $(PERF_EVENTS) -- build/$(PRESET)/bin/m1 --benchmark $(PERF_SCENE) --seed 31

record: m
	perf record -g -- build/$(PRESET)/bin/m1 --benchmark $(PERF_SCENE) --seed 31

clean:
	# Plain clean keeps build trees; clean all also removes generated data
	@if [ -n "$(CLEAN_ALL)" ]; then \
		cmake -E remove_directory build; \
		cmake -E remove_directory results; \
	else \
		cmake --build --preset $(PRESET) --target clean; \
	fi

all:
	@:

slurm:
	@case "$(SLURM_TARGET)" in m0|m1) ;; *) echo 'SLURM_TARGET must be m0 or m1' >&2; exit 2;; esac
	@if [ "$(SLURM_TARGET)" = m1 ]; then proj/m1/slurm.sh; else sbatch proj/m0/m0.slurm; fi
