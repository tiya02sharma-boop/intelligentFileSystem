# ---- Build stage ----
FROM gcc:13 AS builder

WORKDIR /build
COPY backend/ ./

RUN g++ -O2 -std=c++17 -o ifs \
    server.cpp bst.cpp linkedlist.cpp \
    && strip ifs

# ---- Runtime stage ----
FROM debian:bookworm-slim

WORKDIR /app

# Copy compiled binary and frontend
COPY --from=builder /build/ifs ./
COPY frontend/ ./frontend/

# Railway / Render inject $PORT at runtime
ENV PORT=8080

EXPOSE 8080

CMD ["sh", "-c", "./ifs $PORT ./frontend"]
