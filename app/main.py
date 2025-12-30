"""
FastAPI application for Dexcom AWTRIX Bridge.

This module provides HTTP endpoints for glucose data formatted
for AWTRIX3 LED matrix displays.
"""

import logging
import uuid
from contextlib import asynccontextmanager
from datetime import datetime
from typing import Optional

from fastapi import Depends, FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

from .awtrix_formatter import format_for_awtrix
from .config import Settings, get_settings
from .constants import DEFAULT_LOG_FORMAT
from .dexcom_client import DexcomClient, get_dexcom_client
from .exceptions import (
    DexcomAPIError,
    DexcomAuthError,
    DexcomAwtrixError,
    DexcomNoDataError,
)
from .models import AwtrixResponse, GlucoseData, HealthResponse

# =============================================================================
# Logging Configuration
# =============================================================================

logging.basicConfig(
    level=logging.INFO,
    format=DEFAULT_LOG_FORMAT,
)
logger = logging.getLogger(__name__)


# =============================================================================
# Application Lifecycle
# =============================================================================

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan handler for startup and shutdown."""
    logger.info("Dexcom AWTRIX Bridge starting up")
    yield
    logger.info("Dexcom AWTRIX Bridge shutting down")


# =============================================================================
# FastAPI Application
# =============================================================================

app = FastAPI(
    title="Dexcom AWTRIX Bridge",
    description="Fetches Dexcom CGM glucose data and formats for AWTRIX3 LED displays",
    version="2.0.0",
    lifespan=lifespan,
)


# =============================================================================
# Exception Handlers
# =============================================================================

@app.exception_handler(DexcomAwtrixError)
async def dexcom_awtrix_error_handler(
    request: Request,
    exc: DexcomAwtrixError,
) -> JSONResponse:
    """Handle custom application exceptions with structured error responses."""
    logger.error(
        "Application error",
        extra={
            "error_code": exc.error_code.value,
            "message": exc.message,
            "details": exc.details,
            "path": request.url.path,
        }
    )

    status_code = 503 if isinstance(exc, DexcomNoDataError) else 500
    if isinstance(exc, DexcomAuthError):
        status_code = 401

    return JSONResponse(
        status_code=status_code,
        content=exc.to_dict(),
    )


# =============================================================================
# Middleware
# =============================================================================

@app.middleware("http")
async def add_request_context(request: Request, call_next):
    """Add request ID and timing to all requests."""
    request_id = str(uuid.uuid4())[:8]
    start_time = datetime.now()

    # Add request ID to request state
    request.state.request_id = request_id

    response = await call_next(request)

    # Calculate request duration
    duration_ms = (datetime.now() - start_time).total_seconds() * 1000

    # Add response headers
    response.headers["X-Request-ID"] = request_id
    response.headers["X-Response-Time"] = f"{duration_ms:.2f}ms"

    # Log request completion
    logger.info(
        "Request completed",
        extra={
            "request_id": request_id,
            "method": request.method,
            "path": request.url.path,
            "status_code": response.status_code,
            "duration_ms": round(duration_ms, 2),
        }
    )

    return response


# =============================================================================
# Endpoints
# =============================================================================

@app.get("/", response_model=HealthResponse)
async def root():
    """
    Root endpoint - service information.

    Returns service name and status for quick verification.
    """
    return HealthResponse(
        status="healthy",
        service="dexcom-awtrix-bridge",
    )


@app.get("/health")
async def health_check():
    """
    Health check endpoint for container orchestration.

    Returns a simple status for liveness probes (Cloud Run, Kubernetes, etc.).
    """
    return {
        "status": "ok",
        "timestamp": datetime.now().isoformat(),
    }


@app.get("/glucose", response_model=AwtrixResponse)
async def get_glucose_awtrix(
    dexcom_client: DexcomClient = Depends(get_dexcom_client),
    settings: Settings = Depends(get_settings),
):
    """
    Get glucose data formatted for AWTRIX3 custom app.

    This endpoint returns glucose data in a format ready to be pushed
    to an AWTRIX3 device's custom app API.

    **Rate Limiting**: Dexcom API calls are limited to once per 5 minutes.
    Subsequent requests return cached data with a progress bar showing
    countdown to the next refresh.

    **Response Fields**:
    - `text`: Empty (uses draw array for display)
    - `color`: RGB color based on glucose level
    - `draw`: Pixel drawing commands for value, arrow, and delta
    - `progress`: Countdown progress to next refresh (0-100)

    **Example Display**: "149↘-11" (value, trend arrow, delta)
    """
    glucose_data, refresh_progress = dexcom_client.get_current_reading()

    if glucose_data is None:
        raise DexcomNoDataError(
            message="No glucose reading available",
            details="Please ensure your Dexcom sensor is active and connected",
        )

    return format_for_awtrix(glucose_data, settings, refresh_progress)


@app.get("/glucose/raw", response_model=GlucoseData)
async def get_glucose_raw(
    dexcom_client: DexcomClient = Depends(get_dexcom_client),
):
    """
    Get raw glucose data for debugging and custom integrations.

    Returns unformatted glucose data including all available fields
    from the Dexcom API.

    **Response Fields**:
    - `value`: Glucose value in mg/dL
    - `mmol_l`: Glucose value in mmol/L
    - `trend`: Numeric trend indicator (0-9)
    - `trend_direction`: Human-readable trend direction
    - `trend_arrow`: Unicode arrow symbol
    - `delta`: Change from previous reading
    - `timestamp`: ISO 8601 timestamp
    """
    glucose_data, _ = dexcom_client.get_current_reading()

    if glucose_data is None:
        raise DexcomNoDataError(
            message="No glucose reading available",
            details="Please ensure your Dexcom sensor is active and connected",
        )

    return glucose_data


@app.get("/glucose/status")
async def get_glucose_status(
    dexcom_client: DexcomClient = Depends(get_dexcom_client),
):
    """
    Get detailed rate limiting and cache status.

    Useful for debugging and monitoring the bridge's state.

    **Response Fields**:
    - `seconds_until_next_refresh`: Time until next API call allowed
    - `refresh_progress_percent`: Progress bar value (0-100)
    - `can_refresh_now`: Whether a fresh API call will be made
    - `statistics`: API call and cache statistics
    """
    seconds_remaining = dexcom_client.get_seconds_until_next_call()
    progress = dexcom_client.get_refresh_progress()
    statistics = dexcom_client.get_statistics()

    # Calculate next refresh time
    next_refresh = None
    if seconds_remaining > 0:
        from datetime import timedelta
        next_refresh = (datetime.now() + timedelta(seconds=seconds_remaining)).isoformat()

    return {
        "seconds_until_next_refresh": seconds_remaining,
        "refresh_progress_percent": progress,
        "can_refresh_now": seconds_remaining == 0,
        "next_refresh_at": next_refresh,
        "statistics": statistics,
    }


@app.get("/glucose/statistics")
async def get_statistics(
    dexcom_client: DexcomClient = Depends(get_dexcom_client),
):
    """
    Get API usage statistics.

    Provides insights into API performance and cache effectiveness.

    **Response Fields**:
    - `total_api_calls`: Number of actual Dexcom API calls made
    - `cache_hits`: Number of requests served from cache
    - `api_errors`: Number of API errors encountered
    - `cache_hit_rate`: Percentage of requests served from cache
    """
    return dexcom_client.get_statistics()
