# Simulator inputs and where they come from

Everything the simulator produces is **simulated**. These are the published numbers it is built on. Several were read from search snippets because the source PDF could not be opened. Those are marked "snippet": good enough to shape a simulation, not good enough to quote as fact in a case study without opening the source.

| Input | Value used | Source | Confidence |
|-------|-----------|--------|-----------|
| Bengaluru mean daily min / max, Oct | 19.8 / 28.5 °C | IMD Bengaluru climatological table 1991-2020, https://mausam.imd.gov.in/bengaluru/mcdata/Bengaluru.pdf | snippet |
| Nov | 18.3 / 27.4 °C | same | snippet |
| Dec | 16.4 / 26.9 °C | same | snippet |
| Rainy days Oct / Nov / Dec | 9.6 / 4.2 / 1.3 | same | snippet |
| Relative humidity, morning / afternoon | ~87 % / ~55 % | UAS Bangalore GKVK weather normals, https://uasbangalore.edu.in/images/Agrometerology/pdf/gkvk-monthly-weather-normals-2011-2020-converted.pdf | snippet, non-IMD station |
| Sunrise / sunset, Oct avg | 06:10 / 18:02 | https://solarwatch.app/en/sunrise-sunset/india/bengaluru | page |
| Sunrise / sunset, Dec avg | 06:34 / 17:56 | same | page |
| Outdoor CO2 background | 426 ppm (global monthly mean, Jul 2026, preliminary) | NOAA GML, https://gml.noaa.gov/ccgg/trends/global.html | page |
| Urban CO2 above background | +16 to +27 ppm (Seoul, Evanston) → **+20 ppm used** | news report and journal listing | snippet |
| CO2 from one seated adult | ~0.0034 to 0.0058 L/s (Persily equation, 70 kg adult) → **0.005 L/s awake at a desk, 0.0035 L/s asleep** | Persily, ASHRAE Journal 2017, https://pmc.ncbi.nlm.nih.gov/articles/PMC5907511 | page (equation read from a garbled rendering) |
| Air changes per hour, windows shut | 0.16 (London flat) to 0.40 (Danish bedrooms) → **0.3 used** | CLIMA 2022, https://proceedings.open.tudelft.nl/clima2022/article/view/150 ; UCL, https://discovery.ucl.ac.uk/id/eprint/10204238/ | snippet |
| Air changes per hour, window open | 2.9 (small vent) to 19 (all open) → **5 used, varied 3 to 10** | same | snippet |

## Assumptions (not from a source; tune once real data exists)

- Room: a bedroom/study of 3.5 × 3.2 × 2.9 m (about 32.5 m³), one person working from home.
- Time of the daily low and high temperature: about 06:00 and 14:30 (general meteorology, not a sourced Bengaluru value).
- Daily routine, window habits (mosquitoes mean the window often shuts around dusk) and guests are invented, with randomness.
- Indoor temperature follows outdoor with a lag. A running laptop adds about 1 °C.
- Air changes with windows shut: **0.6 per hour**, above the sourced 0.16 to 0.4. Those come from tight European homes, and Indian homes are usually leakier. About 40% of nights the window stays cracked behind a mosquito mesh (1.5 per hour).
