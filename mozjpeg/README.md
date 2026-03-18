# mozjpeg package

## Files
- Dockerfile
- docker-compose.yml
- src/mozjpeg_encode.cpp
- src/mozjpeg_decode.cpp
- src/CMakeLists.txt
- src/mozjpeg_benchmark.py
- ambiguities_report.md

## Build/run examples

### Docker Compose stable
```bash
CHANNEL=stable docker compose build --no-cache mozjpeg-container
docker compose run --rm mozjpeg-container
```

### Docker Compose dev
```bash
CHANNEL=dev docker compose build --no-cache mozjpeg-container
docker compose run --rm mozjpeg-container
```

### Plain Docker stable
```bash
docker build --build-arg CHANNEL=stable -t mozjpeg-container .
docker run --rm -it -v "$(pwd)/results:/workspace/results" -v "$(pwd)/imageset:/workspace/imageset" mozjpeg-container
```

### Plain Docker dev
```bash
docker build --build-arg CHANNEL=dev -t mozjpeg-container .
docker run --rm -it -v "$(pwd)/results:/workspace/results" -v "$(pwd)/imageset:/workspace/imageset" mozjpeg-container
```

## Source selection note
- stable channel: official tag v4.1.5 (GitHub releases page is stale; newer official tags exist)
