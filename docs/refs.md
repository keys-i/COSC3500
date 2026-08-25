# References

Accessed 2026-08-31. A link belongs here only when it informed code, a
scenario, measurement or asset provenance. Private course material stays
private and is not linked, copied or quoted.

## M0 - Topic Selection

| Reference | Use |
| --- | --- |
| [C++ `puts`](https://en.cppreference.com/w/cpp/io/c/puts) | M0 smoke program |
| Private course brief and proposal | Local scope check only |

## M1 - Serial

| Reference | Use |
| --- | --- |
| [FLAME GPU messages](https://docs.flamegpu.com/guide/defining-messages-communication/index.html) | Spatial bins and exact local filtering |
| [FLAME GPU model](https://docs.flamegpu.com/guide/creating-a-model/index.html) | Staged agent-model reference for later M2 boundaries |
| [Gaffer: Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/) | Fixed stepping and render interpolation |
| [Box2D simulation guide](https://box2d.org/documentation/md_simulation.html) | Fixed-step/substep reference |
| [Roofline model](https://escholarship.org/uc/item/78h8v7mr) | Interpreting measured compute and memory limits |
| [Life-like cellular automata](https://ics.uci.edu/~eppstein/ca/lifelike.html) | B/S notation |
| [Natural Earth Admin 0 countries](https://www.naturalearthdata.com/downloads/110m-cultural-vectors/110m-admin-0-countries/) | Country polygons and population estimates for the Chronus SEIR simulation |
| [Natural Earth Admin 1 states and provinces](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-1-states-provinces/) | Real subnational boundaries for regional simulation shading |
| [Survey of India political map](https://surveyofindia.gov.in/pages/political-map-of-india) | Official Government of India boundary perspective used for the 2026 India view |
| [OurAirports data](https://ourairports.com/data/) | Public-domain airport identifiers and present/legacy airport metadata |
| [OpenSky COVID-19 flight dataset](https://doi.org/10.5281/zenodo.5815448) | CC BY airport pairs and flight counts observed in January 2020; coverage is receiver-dependent and is not a published timetable |
| [OpenFlights data](https://openflights.org/data.php) | ODbL fallback pairs where OpenSky has no observed route; historical, not a timetable |
| [OAG Air Travel Statistics 2025](https://www.oag.com/blog/air-travel-statistics-2025) | Global passenger-airline top five by scheduled seats |
| [IATA World Air Transport Statistics](https://www.iata.org/en/services/data/market-data/world-air-transport-statistics/) | Airline passenger and freight ranking basis |
| [2024 freight carrier ranking](https://www.statista.com/statistics/269901/top-10-airlines-worldwide-for-cargo-transported/) | Global cargo-airline top five by freight tonne-kilometres |
| [World Bank Global Shipping Traffic Density](https://datacatalog.worldbank.org/search/dataset/0037580/global-shipping-traffic-density) | CC BY commercial-vessel intensity derived from hourly AIS positions, January 2015–February 2021; aggregate density, not individual voyages |
| [NGA World Port Index](https://msi.nga.mil/Publications/WPI) | Representative port locations for simulated cargo and other vessel routes; not a live shipping feed |
| [UN/LOCODE 2025-1](https://unlocode.unece.org/publications/) | Official trade-and-transport location IDs for ports absent from the current WPI extract |
| [SeaRoute 1.6.0 Marnet](https://github.com/genthalili/searoute-py/releases/tag/1.6.0) | Water-routed shortest-path geometry for visualisation; not navigation or observed AIS tracks |
| [IMO ships' routeing](https://www.imo.org/en/ourwork/safety/pages/shipsrouteing.aspx) | Water-only maritime corridor and routeing-system reference |
| [IMO ship regulations by type](https://www.imo.org/en/ourwork/safety/pages/regulationsdefault.aspx) | Cargo, tanker, bulk and passenger vessel distinctions |
| [UNCTAD Review of Maritime Transport 2025](https://unctad.org/publication/review-maritime-transport-2025) | Container and energy-trade corridor context |
| [WHO COVID-19 timeline](https://www.who.int/news/item/27-04-2020-who-timeline---covid-19) | Wuhan cluster and pandemic milestones |
| [WHO country response case studies](https://www.who.int/publications/i/item/9789240019225) | Government-response feed items across countries and territories |
| [WHO dexamethasone trial response](https://www.who.int/news/item/16-06-2020-who-welcomes-preliminary-results-about-dexamethasone-use-in-treating-critically-ill-covid-19-patients) | June 2020 treatment milestone |
| [WHO first vaccine emergency listing](https://www.who.int/news/item/31-12-2020-who-issues-its-first-emergency-use-validation-for-a-covid-19-vaccine-and-emphasizes-need-for-equitable-global-access) | December 2020 feed milestone |
| [WHO Greek variant labels](https://www.who.int/news/item/31-05-2021-who-announces-simple-easy-to-say-labels-for-sars-cov-2-variants-of-interest-and-concern) | May 2021 Delta-label milestone |
| [WHO Omicron classification](https://www.who.int/news/item/26-11-2021-classification-of-omicron-%28b.1.1.529%29-sars-cov-2-variant-of-concern) | November 2021 feed milestone |
| [Australian border restrictions](https://minister.homeaffairs.gov.au/peterdutton/Pages/border-restrictions.aspx) | March 2020 entry restriction feed item |
| [Australian international reopening](https://minister.homeaffairs.gov.au/KarenAndrews/Pages/reopening-to-tourists-and-other-international-travellers-to-secure-our-economic-recovery.aspx) | November 2021 and February 2022 reopening feed items |
| [India international-flight suspension](https://www.pib.gov.in/PressReleasePage.aspx?PRID=1607257&lang=2&reg=48) | March 2020 scheduled-passenger restriction feed item |
| [India scheduled-flight resumption](https://www.pib.gov.in/PressReleasePage.aspx?PRID=1804014&lang=2&reg=48) | March 2022 reopening feed item |
| [Bangladesh CAAB July 2020 circular](https://new.caab.gov.bd/circul/3373x.pdf) | Limited international-service restart feed item |
| [Bangladesh CAAB April 2022 circular](https://new.caab.gov.bd/circul/AT%20Circular-FSR-02-2022%20%2825APR22UFN%29) | Vaccinated-arrival rule easing feed item |
| [EASA Ukraine conflict-zone bulletin](https://www.easa.europa.eu/en/domains/air-operations/czibs/czib-2022-01r14) | February 2022 civil-airspace closure and continuing route restriction |
| [European Commission 2022 airspace report](https://transport.ec.europa.eu/system/files/2023-10/PRB_Annual_Monitoring_Report_2022.pdf) | EU and Russian reciprocal airspace restrictions |
| [WHO COVID-19 emergency statement](https://www.who.int/azerbaijan/news/item/05-05-2023-statement-on-the-fifteenth-meeting-of-the-international-health-regulations-%282005%29-emergency-committee-regarding-the-coronavirus-disease-%28covid-19%29-pandemic) | May 2023 feed milestone |
| [WHO launches CoViNet](https://www.who.int/news/item/27-03-2024-who-launches-covinet--a-global-network-for-coronaviruses) | March 2024 feed milestone |
| [China government response](https://english.www.gov.cn/news/topnews/202002/10/content_WS5e414765c6d04ea953b7b87b.html) | February 2020 reaction paraphrase |
| [US archived White House remarks](https://trumpwhitehouse.archives.gov/briefings-statements/remarks-president-trump-meeting-african-american-leaders/) | February 2020 reaction paraphrase |
| [Japan COVID-19 response headquarters](https://japan.kantei.go.jp/98_abe/actions/202002/_00042.html) | February 2020 school-closure reaction paraphrase |
| [New Zealand government response](https://www.beehive.govt.nz/release/major-steps-taken-protect-new-zealanders-covid-19) | March 2020 reaction paraphrase |
| [Belarus tractor remarks](https://www.euractiv.com/news/belarusian-leader-proposes-tractor-therapy-for-virus/) | March 2020 reaction paraphrase |
| [France address of 16 March 2020](https://www.elysee.fr/emmanuel-macron/2020/03/16/adresse-aux-francais-covid19) | France reaction paraphrase |
| [Germany address of 18 March 2020](https://www.bundesregierung.de/breg-de/service/newsletter-und-abos/bulletin/ansprache-zur-aktuellen-lage-aufgrund-des-coronavirus-1732746) | Germany reaction paraphrase |
| [Singapore COVID-19 address](https://www.pmo.gov.sg/newsroom/pm-lee-remarks-covid-19-outbreak-12-mar-2020/) | March 2020 reaction paraphrase |
| [India address to the nation](https://www.pmindia.gov.in/en/news_updates/pms-address-to-the-nation-3/) | April 2020 reaction paraphrase |
| [Canada Easter remarks](https://www.pm.gc.ca/en/news/speeches/2020/04/10/prime-ministers-remarks-updating-canadians-covid-19-situation) | April 2020 reaction paraphrase |
| [Australia prime-minister transcript](https://pmtranscripts.pmc.gov.au/release/transcript-42948) | July 2020 reaction paraphrase |
| [South Africa presidency statement](https://thepresidency.gov.za/statement-president-cyril-ramaphosa-progress-national-effort-contain-covid-19-pandemic-union-3) | November 2020 reaction paraphrase |
| [UK coronavirus statement](https://www.gov.uk/government/speeches/pm-statement-at-coronavirus-press-conference-19-july-2021) | July 2021 reaction paraphrase |
| [Tom Hanks and Rita Wilson announcement](https://time.com/5801765/tom-hanks-coronavirus-reactions/) | Public-figure feed item |
| [Arnold Schwarzenegger's stay-home video](https://www.theguardian.com/film/2020/mar/16/arnold-schwarzenegger-and-his-tiny-horses-urges-people-to-stay-home) | Public-figure feed item |
| [Cardi B coronavirus remix](https://www.vogue.com/article/cardi-b-coronavirus-remix) | Public-figure feed item |
| [Dolly Parton's Vanderbilt donation](https://medschool.vanderbilt.edu/basic-sciences/2020/12/18/vanderbilt-researchers-take-leadership-role-in-covid-19-vaccine-development/) | Public-figure feed item |
| [Greta Thunberg at the UN Climate Action Summit](https://www.un.org/development/desa/en/news/sustainable/climate-summit-2019.html) | September 2019 influencer feed item |
| [G20 Osaka Leaders' Declaration](https://www.mofa.go.jp/policy/economy/g20_summit/osaka19/en/documents/final_g20_osaka_leaders_declaration.html) | June 2019 world-news feed item |
| [FIFA Women's World Cup France 2019 final](https://inside.fifa.com/tournaments/womens/womensworldcup/france2019/news/the-uswnt-conquer-lagrandefinale) | July 2019 world-news feed item |
| [NASA Earth Observatory on 2019 Brazil fires](https://earthobservatory.nasa.gov/images/145464/the-world-of-deltas) | August 2019 world-news feed item |
| [John Krasinski's first Some Good News episode](https://www.youtube.com/watch?v=F5pgG1M_h_U) | March 2020 influencer feed item |
| [NASA Demo-2 launch](https://www.nasa.gov/news-release/nasa-astronauts-launch-from-america-in-historic-test-flight-of-spacex-crew-dragon/) | May 2020 world-news feed item |
| [UN Beirut explosion response](https://www.un.org/sg/en/content/sg/statements/2020-08-10/secretary-generals-briefing-the-humanitarian-situation-lebanon-delivered) | August 2020 world-news feed item |
| [NASA Perseverance landing](https://www.nasa.gov/news-release/touchdown-nasas-mars-perseverance-rover-safely-lands-on-red-planet/) | February 2021 world-news feed item |
| [Suez Canal Authority on Ever Given](https://www.suezcanal.gov.eg/English/MediaCenter/News/Pages/31-3-2021.aspx) | March 2021 world-news feed item |
| [IOC Tokyo 2020 closing report](https://newsroom.olympics.com/record/1121) | July 2021 world-news feed item |
| [BTS at the UN SDG Moment](https://unpartnerships.un.org/videos/bts-permission-dance-performed-united-nations) | September 2021 influencer feed item |
| [TeamSeas campaign](https://teamseas.org/terms-of-use/) | October 2021 influencer feed item |
| [UN record of the large-scale attack on Ukraine](https://ukraine.un.org/en/download/188305/210727) | February 2022 world-news feed item |
| [NASA Webb first images](https://science.nasa.gov/mission/webb/webbs-first-images/) | July 2022 world-news feed item |
| [Royal Family announcement of the Queen's death](https://www.royal.uk/announcement-death-queen) | September 2022 world-news feed item |
| [FIFA 2022 final record](https://www.fifa.com/en/articles/world-cup-finals-that-made-history) | December 2022 world-news feed item |
| [UN Türkiye-Syria earthquake response](https://www.un.org/en/turkiye-syria-earthquake-response) | February 2023 world-news feed item |
| [ISRO Chandrayaan-3 mission record](https://www.isro.gov.in/ISRO_EN/Chandrayaan3.html) | August 2023 world-news feed item |
| [UNFCCC COP28 outcomes](https://unfccc.int/cop28/outcomes) | December 2023 world-news feed item |
| [Microsoft CrowdStrike recovery notice](https://techcommunity.microsoft.com/blog/azurecompute/recovery-options-for-azure-virtual-machines-vm-affected-by-crowdstrike-falcon-ag/4196798) | July 2024 world-news feed item |
| [Expo 2025 Osaka opening ceremony](https://www.expo2025.or.jp/en/news/news-20250225-07/) | April 2025 world-news feed item |
| [UNFCCC COP30 record](https://unfccc.int/event/cop-30) | November 2025 world-news feed item |
| [IOC Milano Cortina 2026 closing ceremony](https://newsroom.olympics.com/record/3177/media_id/6838) | February 2026 world-news feed item |
| [NASA Artemis II launch](https://www.nasa.gov/news-release/liftoff-nasa-launches-astronauts-on-historic-artemis-moon-mission/) | April 2026 world-news feed item |
| [FIFA 2026 final standings](https://www.fifa.com/en/tournaments/mens/worldcup/canadamexicousa2026/articles/final-tournament-standings) | July 2026 world-news feed item |
| [Welsh goats during lockdown](https://www.cbsnews.com/news/coronavirus-goats-llandudno-wales-empty-streets-lockdown/) | Funny-news feed item |
| [Shedd Aquarium penguins](https://www.sheddaquarium.org/stories/wellington-edward-and-annie-the-perambulating-penguins) | Funny-news feed item |
| [AP on Moo Deng](https://apnews.com/article/thailand-moo-deng-hippo-khao-kheow-zoo-song-7bd956dd6766d3f1302b3e2c6ac73644) | Thailand funny-news feed item |
| [G20 Bali Leaders' Declaration](https://g20.org/wp-content/uploads/2024/09/2022-11-16-g20-declaration-data.pdf) | Indonesia world-news feed item |
| [Academy Awards 2023](https://www.oscars.org/oscars/ceremonies/2023) | Michelle Yeoh influencer feed item |
| [IOC Tokyo 2020 report](https://library.olympics.com/default/digitalCollection/DigitalCollectionAttachmentDownloadHandler.ashx?documentId=1447353&parentDocumentId=1447343&skipCopyright=true&skipWatermark=true) | Philippines first Olympic gold feed item |
| [Formula 1 Saudi Arabia 2021](https://www.formula1.com/en/racing/2021/saudi-arabia) | Inaugural Saudi Arabian Grand Prix feed item |
| [Recording Academy on Burna Boy](https://www.grammy.com/news/burna-boy-wins-best-global-music-album-twice-tall-2021-grammy-awards-show/) | Nigeria influencer feed item |
| [INEOS 1:59 Challenge](https://www.ineos159challenge.com/) | Eliud Kipchoge influencer feed item |
| [Chile presidential inauguration](https://prensa.presidencia.cl/comunicado.aspx?id=187961+) | Chile government feed item |
| [Peru congressional presidency record](https://www3.congreso.gob.pe/biblioteca/presidentes/2000-2021/) | Peru political-news feed item |
| [ICF carrom rules](https://www.iakc.org/wp-content/uploads/2020/02/Carrom-Official-Rules.pdf) | Rack, striker, queen, pockets and board markings |
| [FIDE Laws of Chess](https://www.fide.com/FIDE/handbook/LawsOfChess.pdf) | Legal moves, castling, en passant, promotion, game completion and clock terminology |
| [CLX](https://github.com/samyeyo/clx) | Pinned Lua 5.5-to-C++ AOT compiler boundary |
| [Pygame documentation](https://www.pygame.org/docs/) | Offline renderer |
| [FFmpeg documentation](https://ffmpeg.org/documentation.html) | Local export and inspection |
| [GCC optimisation options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) | Scalar flags and reports |
| [Clang command-line reference](https://clang.llvm.org/docs/ClangCommandLineReference.html) | Scalar flags and reports |
| [LAMMPS neighbour lists](https://docs.lammps.org/Developer_par_neigh.html) | Cutoff bins, half lists, spatial ordering and displacement-triggered Verlet rebuilds |
| [GROMACS neighbour search](https://manual.gromacs.org/current/reference-manual/algorithms/molecular-dynamics.html) | Buffered Verlet lists and cluster-pair locality |
| [Intel Advisor: optimise memory access patterns](https://www.intel.com/content/www/us/en/docs/advisor/cookbook/2023-1/optimize-memory-access-patterns.html) | Compact traversal locality rationale |
| [Linux Transparent Hugepage documentation](https://docs.kernel.org/admin-guide/mm/transhuge.html) | Requested 2 MiB backing and `/proc/self/smaps` verification boundary |
| [`perf stat`](https://man7.org/linux/man-pages/man1/perf-stat.1.html) | Counter collection where permitted |
| [`wait4(2)`](https://man7.org/linux/man-pages/man2/wait4.2.html) | Per-child resource collection |
| [`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html) | RSS and page-fault field meanings |
| [GitHub Mermaid diagrams](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams) | Deck diagrams in Markdown |
| [Mermaid XY charts](https://mermaid.js.org/syntax/xyChart.html) | Scaling and evidence charts |

## M2 - Parallel

| Reference | Intended use |
| --- | --- |
| [OpenMP specification](https://www.openmp.org/specifications/) | Shared-memory work |
| [MPI standard](https://www.mpi-forum.org/docs/) | Distributed-memory work |
| [CUDA C++ Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/) | GPU work if needed |
| [Slurm `sbatch`](https://slurm.schedmd.com/sbatch.html) and [srun](https://slurm.schedmd.com/srun.html) | Allocation and launch |
| [Nsight Systems](https://docs.nvidia.com/nsight-systems/) | GPU timeline profiling |
| [Nsight Compute](https://docs.nvidia.com/nsight-compute/) | Kernel analysis |

These are M2 references, not evidence that M2 has been implemented.

## Assets and licences

| Imported asset set | Origin and licence | Credit needed |
| --- | --- | --- |
| Chess Pieces and Board Pack | [Joszs](https://joszs.itch.io/chess-pack), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), no generative AI | Yes |
| Tree and Bush Pack | [Joszs](https://joszs.itch.io/foliage-pack), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), no generative AI; dominant mid/foreground foliage | Yes |
| Conway ruined-city backdrop | User-supplied image, blurred and cropped locally; upstream source and licence were not supplied | Verify before publishing |
| Natural Earth Admin 0 countries | [Natural Earth](https://www.naturalearthdata.com/about/terms-of-use/), public domain | No |
| Natural Earth disputed areas | [Natural Earth](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-0-breakaway-disputed-areas/), public domain; selected India-claim polygons rounded to screen precision | No |
| Natural Earth Admin 1 states and provinces | [Natural Earth](https://www.naturalearthdata.com/about/terms-of-use/), public domain; geometry simplified for screen rendering | No |
| Flag Icons 4x3 SVGs | [lipis/flag-icons](https://github.com/lipis/flag-icons), MIT; licence retained with assets | No |
| Airport records | [OurAirports](https://ourairports.com/data/), public domain, plus [OpenFlights](https://openflights.org/data.php), ODbL/DbCL; each airport row preserves both source identifiers | Yes |
| Flight corridors and intensity | [OpenSky COVID-19 flight dataset](https://doi.org/10.5281/zenodo.5815448), CC BY; 447 observed January 2020 pairs plus 153 attributed OpenFlights fallbacks | Yes |
| Port locations | [NGA WPI](https://msi.nga.mil/Publications/WPI), United States Government public-domain data, plus [UN/LOCODE](https://unlocode.unece.org/publications/), CC BY 4.0; each row retains its source ID | Yes |
| Maritime corridor geometry | [SeaRoute 1.6.0 Marnet](https://github.com/genthalili/searoute-py/releases/tag/1.6.0), Apache-2.0; static derived paths only, not the package | Yes |
| Maritime corridor intensity | [World Bank Global Shipping Traffic Density](https://datacatalog.worldbank.org/search/dataset/0037580/global-shipping-traffic-density), CC BY 4.0; route-wise historical commercial AIS-density scores | Yes |

Transport source snapshots refreshed 2026-09-01: OurAirports `airports.csv`
SHA-256 `818b499eb94d9d3d6f660c18495d7b7edcd95f16bc510ccfccb22f130a0c54a3`;
OpenSky `flightlist_20200101_20200131.csv.gz`
`abef6e2a7c58d32eb59bad037bfb43f1bcec4aaad97ad495019239de79408458`;
World Bank `shipdensity_commercial_.zip`
`0cfdce41d9934982bbc8f16bd92ce7545cc088839327cbaa9b0ed1469dda31e8`;
OpenFlights `airports.dat`
SHA-256 `9387cdb38df5bd664da823f8ccb69fdd9b33a1888f5b7cca09c34a3cd9ff59f9`;
OpenFlights `routes.dat`
`bd373706238134f619c624c606dccc74c05c2582a977c489c81de501735f2390`;
NGA WPI CSV
`23bba5f0ce278590c5bccc69c0deb7142087af9a4101600e1264fa062fec52ee`;
UN/LOCODE 2025-1 artifact
`ad409fc7149b10f98d61190c34d9daf78b78bb8b31464cc66de1a89d09b01b5d`;
SeaRoute 1.6.0 `marnet_searoute.geojson`
`111a82d949bb949c396a69a08828781314d3afe779c07e9754c3b2834dfca415`.
The airport, flight and port TSV rows retain the corresponding IDs; flight rows
also retain observed counts and normalized frequency. Sea-route rows retain
the model version, length, named passages, AIS-density score and endpoint snap
distances.

Required credit: “Chess Pieces and Board Pack and Tree and Bush Pack by
Joszs, CC BY 4.0”. Licence texts are retained beside the imported assets.

Supplied Carrom images are visual-analysis references only and are not imported.

## Tooling and supply chain

| Reference | Use |
| --- | --- |
| [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) | Checked-in builds |
| [Ninja manual](https://ninja-build.org/manual.html) | Build execution |
| [uv documentation](https://docs.astral.sh/uv/) | Locked renderer environment |
| [CodeQL for C/C++](https://codeql.github.com/docs/codeql-language-guides/codeql-for-cpp/) | Static analysis |
| [GitHub dependency graph](https://docs.github.com/en/code-security/supply-chain-security/understanding-your-software-supply-chain/about-the-dependency-graph) | Dependency visibility |

No source here grants permission to publish course material or videos.
