PRESET ?= dev
export PRESET
ENGINE ?= m2
MILESTONE ?= m2
SLURM_TARGET ?= m1
SAN ?= au
VALGRIND ?= memcheck
PERF_SCENE ?= templates/conway/1m
PERF_EVENTS ?= cycles,instructions,branches,branch-misses,cache-references,cache-misses
CLEAN_ALL := $(filter all,$(MAKECMDGOALS))

# Keep user-facing targets thin so CI and local runs share the scripts
.PHONY: m m1 m2 lint security valgrind fmt typecheck check cov package \
	profile record clean all slurm help

help:
	@printf '%s\n' \
		'make m ENGINE=m1|m2 | m1 | m2 | fmt | lint | typecheck | check | cov' \
		'make security ENGINE=m1|m2 SAN=m|t|l|au | valgrind ENGINE=m1|m2 VALGRIND=memcheck|cachegrind|callgrind|massif' \
		'make profile ENGINE=m2 PRESET=cluster | package MILESTONE=m1|m2 | clean [all] | slurm SLURM_TARGET=m0|m1'

m:
	@case "$(ENGINE)" in m1|m2) ;; *) echo 'ENGINE must be m1 or m2' >&2; exit 2;; esac
	cmake --preset $(PRESET)
	cmake --build --preset $(PRESET) --target $(ENGINE)

m1:
	$(MAKE) m ENGINE=m1

m2:
	$(MAKE) m ENGINE=m2

security:
	tools/scripts/security san $(SAN) --target $(ENGINE) --preset $(PRESET)

valgrind:
	tools/scripts/security valgrind $(VALGRIND) --target $(ENGINE) --preset $(PRESET)

lint:
	tools/scripts/check lint

fmt:
	tools/scripts/check fmt

typecheck:
	uv run --locked --offline basedpyright

check: fmt lint
	tools/scripts/test.sh $(ENGINE)

cov:
	tools/scripts/check cov

package:
	tools/scripts/package $(MILESTONE)

profile: m
	# Count hardware events outside the timed simulation process
	perf stat -r 5 -e $(PERF_EVENTS) -- build/$(PRESET)/bin/$(ENGINE) --benchmark $(PERF_SCENE) --seed 31

record: m
	perf record -g -- build/$(PRESET)/bin/$(ENGINE) --benchmark $(PERF_SCENE) --seed 31

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
