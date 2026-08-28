FROM python:3.13-slim

RUN apt-get update && apt-get install -y --no-install-recommends openssl ca-certificates \
    && groupadd --gid 10001 sugarfleet \
    && useradd --uid 10001 --gid sugarfleet --no-create-home --shell /usr/sbin/nologin sugarfleet \
    && install -d -o sugarfleet -g sugarfleet /app /data \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --chown=sugarfleet:sugarfleet fleet/requirements.txt /app/fleet/requirements.txt
RUN pip install --no-cache-dir -r /app/fleet/requirements.txt
COPY --chown=sugarfleet:sugarfleet fleet /app/fleet
COPY --chown=sugarfleet:sugarfleet keys /app/keys
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 PORT=8080 FLEET_DATABASE=/data/sugarfleet.db FLEET_OTA_PUBLIC_KEYS_DIR=/app/keys
VOLUME ["/data"]
EXPOSE 8080
USER 10001:10001
HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD ["python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/healthz', timeout=2).read()"]
CMD ["gunicorn", "--bind=0.0.0.0:8080", "--workers=1", "--threads=8", "--timeout=30", "--graceful-timeout=30", "--access-logfile=-", "fleet.sugarfleet.__main__:app"]
