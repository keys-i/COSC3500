PDF = m2.pdf
ROOT = ../..
DEMO_STATE = results/snapshots/predator-prey.csv
DEMO_VIDEO = demo.mp4
DEMO_POSTER = demo-poster.png

$(PDF): m2.tex $(DEMO_POSTER)
	tectonic --outdir . m2.tex
	tectonic --outdir . m2.tex

.PHONY: demo-assets

demo-assets:
	cd $(ROOT) && cmake --preset dev
	cd $(ROOT) && cmake --build --preset dev --target m2
	cd $(ROOT) && build/dev/bin/m2 test/predator-prey --snapshots --seed 31
	rm -f $(ROOT)/results/videos/predator-prey.mp4
	cd $(ROOT) && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy UV_CACHE_DIR=build/uv-cache uv run --locked --offline python -m typer proj.visualiser run render $(DEMO_STATE) --fps 60 --duration 10 --export predator-prey.mp4
	cp $(ROOT)/results/videos/predator-prey.mp4 $(DEMO_VIDEO)
	ffmpeg -y -hide_banner -loglevel error -i $(DEMO_VIDEO) -frames:v 1 $(DEMO_POSTER)

clean:
	rm -f m2.aux m2.log m2.nav m2.out m2.pdf m2.snm m2.toc
