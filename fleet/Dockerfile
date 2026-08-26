FROM python:3.13-slim

RUN apt-get update && apt-get install -y --no-install-recommends openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY fleet/requirements.txt /app/fleet/requirements.txt
RUN pip install --no-cache-dir -r /app/fleet/requirements.txt
COPY fleet /app/fleet
COPY keys /app/keys
ENV PYTHONUNBUFFERED=1 PORT=8080 FLEET_DATABASE=/data/sugarfleet.db FLEET_OTA_PUBLIC_KEYS_DIR=/app/keys
VOLUME ["/data"]
EXPOSE 8080
CMD ["gunicorn", "--bind=0.0.0.0:8080", "--workers=1", "--threads=8", "--access-logfile=-", "fleet.sugarfleet.__main__:app"]
