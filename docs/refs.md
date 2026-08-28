# Source ledger

Links were last checked on 2 September 2026. Each one records a decision,
dataset, event or imported asset used by the project. Course PDFs and supplied
reference images stay private.

## M0

| Source | What it supports |
| --- | --- |
| [C++ `puts`](https://en.cppreference.com/w/cpp/io/c/puts) | Smoke program |
| Private course brief and proposal | Scope check kept outside the repository |

## Engine and measurement

| Source | What it supports |
| --- | --- |
| [FLAME GPU messages](https://docs.flamegpu.com/guide/defining-messages-communication/index.html) | Spatial bins with exact local filtering |
| [FLAME GPU model](https://docs.flamegpu.com/guide/creating-a-model/index.html) | Staged agent boundaries carried into M2 |
| [Gaffer: Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/) | Fixed simulation steps and render interpolation |
| [Box2D simulation guide](https://box2d.org/documentation/md_simulation.html) | Substep behaviour |
| [Roofline model](https://escholarship.org/uc/item/78h8v7mr) | Reading compute and memory limits from measurements |
| [LAMMPS neighbour lists](https://docs.lammps.org/Developer_par_neigh.html) | Cutoff bins, half lists, spatial order and displacement-triggered rebuilds |
| [GROMACS neighbour search](https://manual.gromacs.org/current/reference-manual/algorithms/molecular-dynamics.html) | Buffered Verlet lists and cluster-pair locality |
| [Intel Advisor memory-access guide](https://www.intel.com/content/www/us/en/docs/advisor/cookbook/2023-1/optimize-memory-access-patterns.html) | Compact traversal and locality |
| [GCC optimisation options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) | Release flags and optimisation reports |
| [Clang command-line reference](https://clang.llvm.org/docs/ClangCommandLineReference.html) | Compiler flag behaviour |
| [Linux Transparent Hugepage documentation](https://docs.kernel.org/admin-guide/mm/transhuge.html) | Huge-page requests and the `/proc/self/smaps` proof boundary |
| [`perf stat`](https://man7.org/linux/man-pages/man1/perf-stat.1.html) | Hardware counters where the cluster permits them |
| [`wait4(2)`](https://man7.org/linux/man-pages/man2/wait4.2.html) | Per-child resource collection |
| [`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html) | Resident-memory and page-fault fields |

## Scenario rules and rendering

| Source | What it supports |
| --- | --- |
| [Life-like cellular automata](https://ics.uci.edu/~eppstein/ca/lifelike.html) | Conway B/S notation |
| [ICF carrom rules](https://www.iakc.org/wp-content/uploads/2020/02/Carrom-Official-Rules.pdf) | Rack, striker, queen, pockets and board marks |
| [FIDE Laws of Chess](https://www.fide.com/FIDE/handbook/LawsOfChess.pdf) | Legal moves and terminal results |
| [CLX](https://github.com/samyeyo/clx) | Pinned Lua 5.5-to-C++ compiler boundary |
| [Pygame documentation](https://www.pygame.org/docs/) | Offline rendering |
| [FFmpeg documentation](https://ffmpeg.org/documentation.html) | Local video export and inspection |
| [GitHub Mermaid diagrams](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams) | Markdown diagrams used during presentation work |
| [Mermaid XY charts](https://mermaid.js.org/syntax/xyChart.html) | Early scaling-chart sketches |

## Chronus geography and transport

| Source | What it supports and where the model stops |
| --- | --- |
| [Natural Earth Admin 0 countries](https://www.naturalearthdata.com/downloads/110m-cultural-vectors/110m-admin-0-countries/) | Country geometry and population estimates |
| [Natural Earth Admin 1 states and provinces](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-1-states-provinces/) | Simplified regional shading |
| [Natural Earth disputed areas](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-0-breakaway-disputed-areas/) | India-claim polygons rounded to screen precision |
| [Natural Earth terms](https://www.naturalearthdata.com/about/terms-of-use/) | Public-domain status for the imported boundaries |
| [Survey of India political map](https://surveyofindia.gov.in/pages/political-map-of-india) | Government of India boundary perspective for the 2026 view |
| [OurAirports data](https://ourairports.com/data/) | Public-domain airport IDs and present or legacy metadata |
| [OpenSky COVID-19 flight dataset](https://doi.org/10.5281/zenodo.5815448) | 447 January 2020 airport pairs with observed counts; receiver coverage is incomplete |
| [OpenFlights data](https://openflights.org/data.php) | 753 historical fallback pairs where OpenSky has no observation |
| [OAG Air Travel Statistics 2025](https://www.oag.com/blog/air-travel-statistics-2025) | Passenger-airline ranking by scheduled seats |
| [IATA World Air Transport Statistics](https://www.iata.org/en/services/data/market-data/world-air-transport-statistics/) | Passenger and freight ranking basis |
| [2024 freight carrier ranking](https://www.statista.com/statistics/269901/top-10-airlines-worldwide-for-cargo-transported/) | Freight-carrier ranking by tonne-kilometres |
| [World Bank Global Shipping Traffic Density](https://datacatalog.worldbank.org/search/dataset/0037580/global-shipping-traffic-density) | Historical 2015–2021 AIS-density weights, not individual voyages |
| [NGA World Port Index](https://msi.nga.mil/Publications/WPI) | Representative port positions for simulated routes |
| [UN/LOCODE 2025-1](https://unlocode.unece.org/publications/) | IDs for ports absent from the current WPI extract |
| [SeaRoute 1.6.0 Marnet](https://github.com/genthalili/searoute-py/releases/tag/1.6.0) | Static water-routed geometry, not navigation data |
| [IMO ships' routeing](https://www.imo.org/en/ourwork/safety/pages/shipsrouteing.aspx) | Maritime corridor and routeing-system shapes |
| [IMO ship regulations by type](https://www.imo.org/en/ourwork/safety/pages/regulationsdefault.aspx) | Cargo, tanker, bulk and passenger distinctions |
| [UNCTAD Review of Maritime Transport 2025](https://unctad.org/publication/review-maritime-transport-2025) | Container and energy-trade corridor context |

The 1,200 air agents and 300 ship agents move along attributed corridors, not
live tracks or timetables. The first 150 sea routes carry historical density
weights. Another 150 use WPI endpoints and SeaRoute geometry with
`ais_samples=0` and cadence `0.15`, marking them as modelled connectivity.

## Chronus dated events

Chronus paraphrases these records into short cards. The links anchor the date
and event; they are not copied dialogue.

| Source | Card or simulation event |
| --- | --- |
| [WMO on Hurricane Dorian](https://wmo.int/media/news/hurricane-dorian-causes-devastation-bahamas) | Dorian footprint, September 2019 |
| [WMO on Typhoon Hagibis](https://wmo.int/media/news/tokyo-typhoon-center-turns-30-hagibis-hits-japan) | Hagibis footprint, October 2019 |
| [WMO 2023 extreme-weather summary](https://wmo.int/news/media-centre/climate-change-indicators-reached-record-levels-2023-wmo) | Freddy, Mocha and Otis footprints |
| [WHO COVID-19 timeline](https://www.who.int/news/item/27-04-2020-who-timeline---covid-19) | Wuhan cluster and early pandemic chronology |
| [WHO country response case studies](https://www.who.int/publications/i/item/9789240019225) | Cross-country response dates |
| [WHO dexamethasone trial response](https://www.who.int/news/item/16-06-2020-who-welcomes-preliminary-results-about-dexamethasone-use-in-treating-critically-ill-covid-19-patients) | Dexamethasone result, June 2020 |
| [WHO first vaccine emergency listing](https://www.who.int/news/item/31-12-2020-who-issues-its-first-emergency-use-validation-for-a-covid-19-vaccine-and-emphasizes-need-for-equitable-global-access) | First emergency vaccine listing, December 2020 |
| [WHO Greek variant labels](https://www.who.int/news/item/31-05-2021-who-announces-simple-easy-to-say-labels-for-sars-cov-2-variants-of-interest-and-concern) | Delta naming, May 2021 |
| [WHO Omicron classification](https://www.who.int/news/item/26-11-2021-classification-of-omicron-%28b.1.1.529%29-sars-cov-2-variant-of-concern) | Omicron classification, November 2021 |
| [WHO COVID-19 emergency statement](https://www.who.int/azerbaijan/news/item/05-05-2023-statement-on-the-fifteenth-meeting-of-the-international-health-regulations-%282005%29-emergency-committee-regarding-the-coronavirus-disease-%28covid-19%29-pandemic) | End of the global health emergency, May 2023 |
| [WHO launches CoViNet](https://www.who.int/news/item/27-03-2024-who-launches-covinet--a-global-network-for-coronaviruses) | CoViNet launch, March 2024 |
| [Australian border restrictions](https://minister.homeaffairs.gov.au/peterdutton/Pages/border-restrictions.aspx) | Entry restriction, March 2020 |
| [Australian international reopening](https://minister.homeaffairs.gov.au/KarenAndrews/Pages/reopening-to-tourists-and-other-international-travellers-to-secure-our-economic-recovery.aspx) | Reopening stages, November 2021 and February 2022 |
| [India international-flight suspension](https://www.pib.gov.in/PressReleasePage.aspx?PRID=1607257&lang=2&reg=48) | Scheduled-passenger suspension, March 2020 |
| [India scheduled-flight resumption](https://www.pib.gov.in/PressReleasePage.aspx?PRID=1804014&lang=2&reg=48) | Scheduled-flight return, March 2022 |
| [Bangladesh CAAB July 2020 circular](https://new.caab.gov.bd/circul/3373x.pdf) | Limited international service restart |
| [Bangladesh CAAB April 2022 circular](https://new.caab.gov.bd/circul/AT%20Circular-FSR-02-2022%20%2825APR22UFN%29) | Vaccinated-arrival easing |
| [EASA Ukraine conflict-zone bulletin](https://www.easa.europa.eu/en/domains/air-operations/czibs/czib-2022-01r14) | Civil-airspace closure from February 2022 |
| [EASA active conflict-zone bulletins](https://www.easa.europa.eu/en/conflict-zone-status/active) | Ukraine, Russia, Sudan and Middle East route restrictions |
| [European Commission 2022 airspace report](https://transport.ec.europa.eu/system/files/2023-10/PRB_Annual_Monitoring_Report_2022.pdf) | EU and Russian reciprocal restrictions |
| [IMO Strait of Hormuz safe-passage response](https://www.imo.org/en/mediacentre/pressbriefings/pages/imo-calls-for-safe-passage-framework-in-strait-of-hormuz.aspx) | March 2026 shutdown and trapped vessels |
| [IMO August 2026 Hormuz update](https://www.imo.org/en/mediacentre/pressbriefings/pages/statement-on-the-ongoing-crisis-in-the-strait-of-hormuz.aspx) | Continuing vessel and seafarer disruption |
| [China government response](https://english.www.gov.cn/news/topnews/202002/10/content_WS5e414765c6d04ea953b7b87b.html) | February 2020 public response |
| [US archived White House remarks](https://trumpwhitehouse.archives.gov/briefings-statements/remarks-president-trump-meeting-african-american-leaders/) | February 2020 presidential remarks |
| [Japan COVID-19 response headquarters](https://japan.kantei.go.jp/98_abe/actions/202002/_00042.html) | School-closure decision |
| [New Zealand government response](https://www.beehive.govt.nz/release/major-steps-taken-protect-new-zealanders-covid-19) | March 2020 protective measures |
| [Belarus tractor remarks](https://www.euractiv.com/news/belarusian-leader-proposes-tractor-therapy-for-virus/) | March 2020 tractor comments |
| [France address of 16 March 2020](https://www.elysee.fr/emmanuel-macron/2020/03/16/adresse-aux-francais-covid19) | National address |
| [Germany address of 18 March 2020](https://www.bundesregierung.de/breg-de/service/newsletter-und-abos/bulletin/ansprache-zur-aktuellen-lage-aufgrund-des-coronavirus-1732746) | Televised address |
| [Singapore COVID-19 address](https://www.pmo.gov.sg/newsroom/pm-lee-remarks-covid-19-outbreak-12-mar-2020/) | Outbreak speech |
| [India address to the nation](https://www.pmindia.gov.in/en/news_updates/pms-address-to-the-nation-3/) | April 2020 national address |
| [Canada Easter remarks](https://www.pm.gc.ca/en/news/speeches/2020/04/10/prime-ministers-remarks-updating-canadians-covid-19-situation) | Easter update |
| [Australia prime-minister transcript](https://pmtranscripts.pmc.gov.au/release/transcript-42948) | July 2020 briefing |
| [South Africa presidency statement](https://thepresidency.gov.za/statement-president-cyril-ramaphosa-progress-national-effort-contain-covid-19-pandemic-union-3) | November 2020 containment update |
| [UK coronavirus statement](https://www.gov.uk/government/speeches/pm-statement-at-coronavirus-press-conference-19-july-2021) | July 2021 press conference |
| [Tom Hanks and Rita Wilson announcement](https://time.com/5801765/tom-hanks-coronavirus-reactions/) | Public diagnosis announcement |
| [Arnold Schwarzenegger's stay-home video](https://www.theguardian.com/film/2020/mar/16/arnold-schwarzenegger-and-his-tiny-horses-urges-people-to-stay-home) | Stay-home video with Whiskey and Lulu |
| [Cardi B coronavirus remix](https://www.vogue.com/article/cardi-b-coronavirus-remix) | Viral remix |
| [Dolly Parton's Vanderbilt donation](https://medschool.vanderbilt.edu/basic-sciences/2020/12/18/vanderbilt-researchers-take-leadership-role-in-covid-19-vaccine-development/) | Vaccine-research donation |
| [Greta Thunberg at the UN Climate Action Summit](https://www.un.org/development/desa/en/news/sustainable/climate-summit-2019.html) | September 2019 summit speech |
| [G20 Osaka Leaders' Declaration](https://www.mofa.go.jp/policy/economy/g20_summit/osaka19/en/documents/final_g20_osaka_leaders_declaration.html) | June 2019 Osaka summit |
| [FIFA Women's World Cup France 2019 final](https://inside.fifa.com/tournaments/womens/womensworldcup/france2019/news/the-uswnt-conquer-lagrandefinale) | July 2019 final |
| [NASA Earth Observatory on 2019 Brazil fires](https://earthobservatory.nasa.gov/images/145464/the-world-of-deltas) | August 2019 Brazil fire coverage |
| [John Krasinski's first Some Good News episode](https://www.youtube.com/watch?v=F5pgG1M_h_U) | March 2020 first episode |
| [NASA Demo-2 launch](https://www.nasa.gov/news-release/nasa-astronauts-launch-from-america-in-historic-test-flight-of-spacex-crew-dragon/) | May 2020 crewed launch |
| [UN Beirut explosion response](https://www.un.org/sg/en/content/sg/statements/2020-08-10/secretary-generals-briefing-the-humanitarian-situation-lebanon-delivered) | August 2020 humanitarian response |
| [NASA Perseverance landing](https://www.nasa.gov/news-release/touchdown-nasas-mars-perseverance-rover-safely-lands-on-red-planet/) | February 2021 Mars landing |
| [Suez Canal Authority on Ever Given](https://www.suezcanal.gov.eg/English/MediaCenter/News/Pages/31-3-2021.aspx) | March 2021 canal reopening |
| [IOC Tokyo 2020 closing report](https://newsroom.olympics.com/record/1121) | July 2021 closing record |
| [BTS at the UN SDG Moment](https://unpartnerships.un.org/videos/bts-permission-dance-performed-united-nations) | September 2021 UN performance |
| [TeamSeas campaign](https://teamseas.org/terms-of-use/) | October 2021 campaign |
| [UN record of the large-scale attack on Ukraine](https://ukraine.un.org/en/download/188305/210727) | February 2022 invasion |
| [NASA Webb first images](https://science.nasa.gov/mission/webb/webbs-first-images/) | July 2022 image release |
| [Royal Family announcement of the Queen's death](https://www.royal.uk/announcement-death-queen) | September 2022 announcement |
| [FIFA 2022 final record](https://www.fifa.com/en/articles/world-cup-finals-that-made-history) | December 2022 final |
| [UN Türkiye-Syria earthquake response](https://www.un.org/en/turkiye-syria-earthquake-response) | February 2023 earthquake response |
| [ISRO Chandrayaan-3 mission record](https://www.isro.gov.in/ISRO_EN/Chandrayaan3.html) | August 2023 lunar landing |
| [UNFCCC COP28 outcomes](https://unfccc.int/cop28/outcomes) | December 2023 conference outcome |
| [Microsoft CrowdStrike recovery notice](https://techcommunity.microsoft.com/blog/azurecompute/recovery-options-for-azure-virtual-machines-vm-affected-by-crowdstrike-falcon-ag/4196798) | July 2024 outage recovery |
| [Expo 2025 Osaka opening ceremony](https://www.expo2025.or.jp/en/news/news-20250225-07/) | April 2025 opening |
| [UNFCCC COP30 record](https://unfccc.int/event/cop-30) | November 2025 conference |
| [IOC Milano Cortina 2026 closing ceremony](https://newsroom.olympics.com/record/3177/media_id/6838) | February 2026 closing ceremony |
| [NASA Artemis II launch](https://www.nasa.gov/news-release/liftoff-nasa-launches-astronauts-on-historic-artemis-moon-mission/) | April 2026 launch |
| [FIFA 2026 final standings](https://www.fifa.com/en/tournaments/mens/worldcup/canadamexicousa2026/articles/final-tournament-standings) | July 2026 final |
| [Welsh goats during lockdown](https://www.cbsnews.com/news/coronavirus-goats-llandudno-wales-empty-streets-lockdown/) | Llandudno goats |
| [Shedd Aquarium penguins](https://www.sheddaquarium.org/stories/wellington-edward-and-annie-the-perambulating-penguins) | Wellington, Edward and Annie exploring the aquarium |
| [AP on Moo Deng](https://apnews.com/article/thailand-moo-deng-hippo-khao-kheow-zoo-song-7bd956dd6766d3f1302b3e2c6ac73644) | Moo Deng coverage |
| [G20 Bali Leaders' Declaration](https://g20.org/wp-content/uploads/2024/09/2022-11-16-g20-declaration-data.pdf) | Indonesia card |
| [Academy Awards 2023](https://www.oscars.org/oscars/ceremonies/2023) | Michelle Yeoh card |
| [IOC Tokyo 2020 report](https://library.olympics.com/default/digitalCollection/DigitalCollectionAttachmentDownloadHandler.ashx?documentId=1447353&parentDocumentId=1447343&skipCopyright=true&skipWatermark=true) | Philippines' first Olympic gold |
| [Formula 1 Saudi Arabia 2021](https://www.formula1.com/en/racing/2021/saudi-arabia) | Inaugural Saudi Arabian Grand Prix |
| [Recording Academy on Burna Boy](https://www.grammy.com/news/burna-boy-wins-best-global-music-album-twice-tall-2021-grammy-awards-show/) | Burna Boy's Grammy win |
| [INEOS 1:59 Challenge](https://www.ineos159challenge.com/) | Eliud Kipchoge's sub-two-hour marathon |
| [Chile presidential inauguration](https://prensa.presidencia.cl/comunicado.aspx?id=187961+) | Presidential inauguration |
| [Peru congressional presidency record](https://www3.congreso.gob.pe/biblioteca/presidentes/2000-2021/) | Congressional presidency chronology |

## M2 reading list

| Source | Planned use |
| --- | --- |
| [OpenMP specification](https://www.openmp.org/specifications/) | Shared-memory work |
| [MPI standard](https://www.mpi-forum.org/docs/) | Distributed-memory work |
| [CUDA C++ Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/) | GPU work if the measured design needs it |
| [Slurm `sbatch`](https://slurm.schedmd.com/sbatch.html) and [`srun`](https://slurm.schedmd.com/srun.html) | Allocation and process launch |
| [Nsight Systems](https://docs.nvidia.com/nsight-systems/) | GPU timeline profiling |
| [Nsight Compute](https://docs.nvidia.com/nsight-compute/) | Kernel analysis |

This is a reading list, not a claim that M2 exists.

## Imported visuals

| Asset | Origin and licence | Credit |
| --- | --- | --- |
| Chess Pieces and Board Pack; Tree and Bush Pack | [Joszs chess](https://joszs.itch.io/chess-pack), [Joszs foliage](https://joszs.itch.io/foliage-pack), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); both source pages state no generative AI | Required |
| Flag Icons 4×3 SVGs | [lipis/flag-icons](https://github.com/lipis/flag-icons), MIT | Not required |
| Conway ruined-city backdrop | User-supplied image, blurred and cropped locally; upstream source and licence were not supplied | Verify before publishing |

The required credit is “Chess Pieces and Board Pack and Tree and Bush Pack by
Joszs, CC BY 4.0”. Licence files sit beside the imports. Supplied carrom images
were used for visual comparison only and were not imported.

## Dataset snapshots

Transport inputs were refreshed on 1 September 2026. Rows retain source IDs;
flight rows also retain observed counts and normalised frequency, while sea
routes retain model version, length, named passages, density score and endpoint
snap distance.

| Artifact | SHA-256 |
| --- | --- |
| OurAirports `airports.csv` | `818b499eb94d9d3d6f660c18495d7b7edcd95f16bc510ccfccb22f130a0c54a3` |
| OpenSky `flightlist_20200101_20200131.csv.gz` | `abef6e2a7c58d32eb59bad037bfb43f1bcec4aaad97ad495019239de79408458` |
| World Bank `shipdensity_commercial_.zip` | `0cfdce41d9934982bbc8f16bd92ce7545cc088839327cbaa9b0ed1469dda31e8` |
| OpenFlights `airports.dat` | `9387cdb38df5bd664da823f8ccb69fdd9b33a1888f5b7cca09c34a3cd9ff59f9` |
| OpenFlights `routes.dat` | `bd373706238134f619c624c606dccc74c05c2582a977c489c81de501735f2390` |
| NGA WPI current CSV | `23bba5f0ce278590c5bccc69c0deb7142087af9a4101600e1264fa062fec52ee` |
| NGA WPI archived 2019 shapefile | `0717cdace0d446b8b16be35c94cc1c4136f23157d8268a8e52b957797cb357ae` |
| UN/LOCODE 2025-1 artifact | `ad409fc7149b10f98d61190c34d9daf78b78bb8b31464cc66de1a89d09b01b5d` |
| SeaRoute 1.6.0 `marnet_searoute.geojson` | `111a82d949bb949c396a69a08828781314d3afe779c07e9754c3b2834dfca415` |

## Toolchain

| Source | What it supports |
| --- | --- |
| [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) | Checked-in build definitions |
| [Ninja manual](https://ninja-build.org/manual.html) | Build execution |
| [uv documentation](https://docs.astral.sh/uv/) | Locked renderer environment |
| [CodeQL for C/C++](https://codeql.github.com/docs/codeql-language-guides/codeql-for-cpp/) | Static analysis |
| [GitHub dependency graph](https://docs.github.com/en/code-security/supply-chain-security/understanding-your-software-supply-chain/about-the-dependency-graph) | Dependency visibility |
