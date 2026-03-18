# jpegli package

## Files
- Dockerfile
- docker-compose.yml
- src/jpegli_encode.cpp
- src/jpegli_decode.cpp
- src/CMakeLists.txt
- src/jpegli_benchmark.py
- ambiguities_report.md

## Build/run examples

### Docker Compose stable
```bash
CHANNEL=stable docker compose build --no-cache jpegli-container
docker compose run --rm jpegli-container
```

### Docker Compose dev
```bash
CHANNEL=dev docker compose build --no-cache jpegli-container
docker compose run --rm jpegli-container
```

### Plain Docker stable
```bash
docker build --build-arg CHANNEL=stable -t jpegli-container .
docker run --rm -it -v "$(pwd)/results:/workspace/results" -v "$(pwd)/imageset:/workspace/imageset" jpegli-container
```

### Plain Docker dev
```bash
docker build --build-arg CHANNEL=dev -t jpegli-container .
docker run --rm -it -v "$(pwd)/results:/workspace/results" -v "$(pwd)/imageset:/workspace/imageset" jpegli-container
```

## Source selection note
- stable/dev channels both use the official public main branch because no official releases are published
