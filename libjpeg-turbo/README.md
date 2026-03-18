# libjpeg-turbo package

## Files
- Dockerfile
- docker-compose.yml
- src/libjpeg_turbo_encode.cpp
- src/libjpeg_turbo_decode.cpp
- src/CMakeLists.txt
- src/libjpeg_turbo_benchmark.py
- ambiguities_report.md

## Build/run examples

### Docker Compose stable
```bash
CHANNEL=stable docker compose build --no-cache libjpeg-turbo-container
docker compose run --rm libjpeg-turbo-container
```

### Docker Compose dev
```bash
CHANNEL=dev docker compose build --no-cache libjpeg-turbo-container
docker compose run --rm libjpeg-turbo-container
```

### Plain Docker stable
```bash
docker build --build-arg CHANNEL=stable -t libjpeg-turbo-container .
docker run --rm -it -v "$(pwd)/results:/workspace/results" -v "$(pwd)/imageset:/workspace/imageset" libjpeg-turbo-container
```

### Plain Docker dev
```bash
docker build --build-arg CHANNEL=dev -t libjpeg-turbo-container .
docker run --rm -it -v "$(pwd)/results:/workspace/results" -v "$(pwd)/imageset:/workspace/imageset" libjpeg-turbo-container
```

## Source selection note
- stable channel: official tag 3.1.3
