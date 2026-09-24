# Debian 12 (Bookworm) is a stable Linux base with glibc and epoll support.
FROM debian:12-slim AS build

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        make \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN make clean && make && make test

FROM debian:12-slim AS runtime

WORKDIR /app
COPY --from=build /app/server ./server

RUN useradd --system --no-create-home --uid 10001 echo \
    && chown echo:echo /app/server

USER echo
EXPOSE 8080

ENTRYPOINT ["./server"]
CMD ["0.0.0.0", "8080"]
