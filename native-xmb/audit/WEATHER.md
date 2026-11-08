# Network > Weather (2026-11-07)

A native page (not a firmware surface; plan item P5). Code: `src/weather_client.*`, `src/weather_shell.cpp`.

- **Setup**: `<data>/weather.json` with the place, e.g. `{"place": "Chicago, IL"}`; add `"units": "metric"` for Celsius.
  The place is geocoded once through Open-Meteo and stored with its coordinates. No API key; no IP geolocation.
- **Row**: "<temperature> <conditions> - <place>" with a conditions icon (day/night), refreshed every 15 minutes (the
  client serves its cache inside that window).
- **Page**: Now (feels like, humidity, wind), Today and the next four days (high/low, rain chance, sunrise/sunset for
  today), and Units (Cross switches Fahrenheit/Celsius).
- **Offline**: the last forecast (`<data>/weather/last.json`) is shown with its age.

Verified 2026-11-07 against live Open-Meteo for Chicago, IL: "39°F Clear", five days with icons.
