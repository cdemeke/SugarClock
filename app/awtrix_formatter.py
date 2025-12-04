from typing import List, Optional, Tuple

from .config import Settings
from .models import AwtrixResponse, GlucoseData


def get_color_for_glucose(value: int, settings: Settings) -> List[int]:
    """
    Returns RGB color based on glucose value using configurable colors.

    Color coding (configurable via env):
    - Red: <70 mg/dL (hypoglycemia)
    - Green: 70-180 mg/dL (normal range)
    - Yellow: 181-240 mg/dL (high)
    - Orange: >240 mg/dL (very high)
    """
    if value < settings.glucose_critical_low:
        return settings.parse_color(settings.color_critical_low)
    elif value < settings.glucose_low:
        return settings.parse_color(settings.color_low)
    elif value <= settings.glucose_high:
        return settings.parse_color(settings.color_normal)
    elif value <= settings.glucose_very_high:
        return settings.parse_color(settings.color_high)
    else:
        return settings.parse_color(settings.color_very_high)


def get_background_color(value: int, settings: Settings) -> Optional[List[int]]:
    """Returns optional background color for critical values."""
    if value < settings.glucose_critical_low:
        # Critical low - dim red background
        return [64, 0, 0]
    elif value > settings.glucose_very_high:
        # Critical high - dim orange background
        return [64, 32, 0]
    return None


def rgb_to_hex(rgb: List[int]) -> str:
    """Convert RGB array to hex color string for draw commands."""
    return "#{:02x}{:02x}{:02x}".format(rgb[0], rgb[1], rgb[2])


def get_arrow_drawing(delta: Optional[int], settings: Settings, hex_color: str, x: int) -> Tuple[List[dict], int]:
    """
    Returns draw commands for arrow at specified x position.

    Arrow types based on delta:
    - Stable (→) for delta ±0 to ±stable_threshold
    - Diagonal (↗ ↘) for moderate changes
    - Vertical (↑ ↓) for rapid changes

    Returns: (draw_commands, arrow_width)
    """
    abs_delta = abs(delta) if delta is not None else 0

    # Stable: horizontal arrow (→)
    if delta is None or abs_delta <= settings.delta_stable_threshold:
        return [
            {"dl": [x, 3, x + 3, 3, hex_color]},      # Horizontal stem
            {"dp": [x + 4, 3, hex_color]},            # Tip
            {"dp": [x + 3, 2, hex_color]},            # Head top
            {"dp": [x + 3, 4, hex_color]},            # Head bottom
        ], 5

    # Rapid: vertical arrows (↑ ↓)
    elif abs_delta > settings.delta_rapid_threshold:
        if delta > 0:
            # Up arrow (↑)
            return [
                {"dl": [x + 2, 2, x + 2, 6, hex_color]},  # Stem
                {"dp": [x + 2, 1, hex_color]},            # Tip
                {"dp": [x + 1, 2, hex_color]},            # Head left inner
                {"dp": [x + 3, 2, hex_color]},            # Head right inner
                {"dp": [x, 3, hex_color]},                # Head left outer
                {"dp": [x + 4, 3, hex_color]},            # Head right outer
            ], 5
        else:
            # Down arrow (↓) - user's design
            return [
                {"dl": [x + 2, 0, x + 2, 5, hex_color]},  # Stem
                {"dp": [x + 2, 6, hex_color]},            # Tip
                {"dp": [x + 1, 5, hex_color]},            # Head left inner
                {"dp": [x + 3, 5, hex_color]},            # Head right inner
                {"dp": [x, 4, hex_color]},                # Head left outer
                {"dp": [x + 4, 4, hex_color]},            # Head right outer
            ], 5

    # Moderate: diagonal arrows (↗ ↘)
    else:
        if delta > 0:
            # Diagonal up-right (↗)
            return [
                {"dp": [x, 5, hex_color]},
                {"dp": [x + 1, 4, hex_color]},
                {"dp": [x + 2, 3, hex_color]},
                {"dp": [x + 3, 2, hex_color]},
                {"dp": [x + 4, 1, hex_color]},
                {"dp": [x + 4, 0, hex_color]},            # Tip top
                {"dp": [x + 3, 0, hex_color]},            # Head left
                {"dp": [x + 4, 2, hex_color]},            # Head down
            ], 5
        else:
            # Diagonal down-right (↘) - user's design
            return [
                {"dp": [x, 1, hex_color]},
                {"dp": [x + 1, 2, hex_color]},
                {"dp": [x + 2, 3, hex_color]},
                {"dp": [x + 3, 4, hex_color]},
                {"dp": [x + 4, 5, hex_color]},
                {"dp": [x + 4, 6, hex_color]},            # Tip bottom
                {"dp": [x + 3, 6, hex_color]},            # Head left
                {"dp": [x + 4, 4, hex_color]},            # Head up
            ], 5


def get_indicator_dot_drawing(
    refresh_progress: int, settings: Settings
) -> List[dict]:
    """
    Returns draw commands for a growing/blinking indicator dot.

    The dot grows in size approximately every 30 seconds during the 5-minute
    API polling interval, providing visual feedback that the system is
    preparing to fetch new data.

    Args:
        refresh_progress: Progress percentage (0-100) towards next API refresh
        settings: Application settings with dot configuration

    Returns:
        List of draw commands for the indicator dot
    """
    if not settings.indicator_dot_enabled:
        return []

    # Calculate dot size based on progress (grows every ~30 seconds)
    # 5 minutes = 300 seconds, 30 second intervals = 10 growth steps
    # Progress 0-100 maps to steps 0-10
    growth_step = refresh_progress // 10  # 0-10 steps

    # Alternate between bright and dim color for blink effect
    # Blinks every other growth step
    is_bright = (growth_step % 2) == 0
    color_str = settings.indicator_dot_color if is_bright else settings.indicator_dot_color_dim
    color_hex = rgb_to_hex(settings.parse_color(color_str))

    # Base position (top-right corner, grows downward and leftward)
    base_x = settings.indicator_dot_x
    base_y = settings.indicator_dot_y

    draw_commands = []

    # Dot grows based on progress:
    # Step 0 (0-9%): 1 pixel
    # Step 1-2 (10-29%): 2 pixels (vertical line)
    # Step 3-4 (30-49%): 3 pixels (L shape)
    # Step 5-6 (50-69%): 4 pixels (square)
    # Step 7-8 (70-89%): 5 pixels (plus sign)
    # Step 9-10 (90-100%): 6 pixels (larger shape)

    if growth_step >= 0:
        # Always show at least 1 pixel
        draw_commands.append({"dp": [base_x, base_y, color_hex]})

    if growth_step >= 1:
        # Add pixel below
        draw_commands.append({"dp": [base_x, base_y + 1, color_hex]})

    if growth_step >= 2:
        # Add pixel to the left
        draw_commands.append({"dp": [base_x - 1, base_y, color_hex]})

    if growth_step >= 3:
        # Add pixel diagonally
        draw_commands.append({"dp": [base_x - 1, base_y + 1, color_hex]})

    if growth_step >= 4:
        # Add another pixel extending down
        draw_commands.append({"dp": [base_x, base_y + 2, color_hex]})

    if growth_step >= 5:
        # Add pixel extending left
        draw_commands.append({"dp": [base_x - 1, base_y + 2, color_hex]})

    if growth_step >= 6:
        # Add more pixels for larger shape
        draw_commands.append({"dp": [base_x - 2, base_y, color_hex]})

    if growth_step >= 7:
        draw_commands.append({"dp": [base_x - 2, base_y + 1, color_hex]})

    if growth_step >= 8:
        draw_commands.append({"dp": [base_x - 2, base_y + 2, color_hex]})

    if growth_step >= 9:
        # Final growth - add top row
        draw_commands.append({"dp": [base_x, base_y + 3, color_hex]})
        draw_commands.append({"dp": [base_x - 1, base_y + 3, color_hex]})
        draw_commands.append({"dp": [base_x - 2, base_y + 3, color_hex]})

    return draw_commands


def estimate_text_width(text: str) -> int:
    """Estimate pixel width of text in AWTRIX3 default font."""
    width = 0
    for char in text:
        if char in '1il:':
            width += 2
        elif char in ' ':
            width += 2
        elif char in '+-':
            width += 4
        else:
            width += 4
        width += 1
    return width - 1 if width > 0 else 0


def format_delta(delta: Optional[int]) -> str:
    """Format delta with sign."""
    if delta is None:
        return ""
    if delta >= 0:
        return f"+{delta}"
    else:
        return str(delta)


def format_for_awtrix(
    glucose_data: GlucoseData, settings: Settings, refresh_progress: int = 0
) -> AwtrixResponse:
    """
    Format glucose data for AWTRIX3 custom app.

    Display format: {value}{arrow}{delta} (e.g., "149↘-11")

    The arrow is pixel-drawn inline between the glucose value and delta.

    Args:
        glucose_data: Current glucose reading
        settings: Application settings
        refresh_progress: Progress towards next API refresh (0-100).
                         0 = just refreshed, 100 = ready to refresh again.
                         Displayed as a progress bar at bottom of screen.
    """
    value = glucose_data.value
    delta = glucose_data.delta

    # Get colors (configurable via env)
    glucose_color = get_color_for_glucose(value, settings)
    delta_color = settings.parse_color(settings.color_delta)
    progress_color = settings.parse_color(settings.color_progress_bar)
    progress_bg_color = settings.parse_color(settings.color_progress_bg)

    # Convert to hex for draw commands
    glucose_hex = rgb_to_hex(glucose_color)
    delta_hex = rgb_to_hex(delta_color)

    # Format strings
    value_str = str(value)
    delta_str = format_delta(delta)

    # Calculate positions: "149" + arrow + "-11"
    value_width = estimate_text_width(value_str)
    arrow_x = value_width + 1  # After value + gap

    # Get arrow drawing at calculated position
    arrow_draw, arrow_width = get_arrow_drawing(delta, settings, glucose_hex, arrow_x)

    # Delta position after arrow
    delta_x = arrow_x + arrow_width + 1

    # Build draw commands: value + arrow + delta
    draw_commands = [
        {"dt": [0, 0, value_str, glucose_hex]},  # Glucose value
    ]
    draw_commands.extend(arrow_draw)  # Arrow pixels
    if delta_str:
        draw_commands.append({"dt": [delta_x, 0, delta_str, delta_hex]})  # Delta

    # Add growing indicator dot (shows API refresh progress)
    indicator_dot_commands = get_indicator_dot_drawing(refresh_progress, settings)
    draw_commands.extend(indicator_dot_commands)

    # Build response
    response = AwtrixResponse(
        text="",  # Empty - using draw array
        color=glucose_color,
        icon=None,
        duration=settings.awtrix_duration,
        noScroll=True,
        center=False,
        lifetime=settings.awtrix_lifetime,
        draw=draw_commands,
    )

    # Add background for critical values
    background = get_background_color(value, settings)
    if background:
        response.background = background

    # Add blinking for critical lows
    if value < settings.glucose_critical_low:
        response.blinkText = 500  # Blink every 500ms

    # Add progress bar
    response.progress = refresh_progress
    response.progressC = progress_color
    response.progressBC = progress_bg_color

    return response
