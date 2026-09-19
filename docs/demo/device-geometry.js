/** TC001 front elevation. Overall dimensions published by Ulanzi.
 * Active matrix width and bezel inset estimated from its linked front photograph:
 * https://blakadder.com/assets/images/ulanzi-tc001/pixelrows.jpg
 * They are photographic estimates, not manufacturer mechanical tolerances.
 */
export const DEVICE_GEOMETRY = Object.freeze({
  widthMm: 200.58,
  heightMm: 70.25,
  columns: 32,
  rows: 8,
  matrixWidthFraction: 0.862,
  rimWidthFraction: 0.007,
  cornerRadiusMm: 7,
  pixelFillFraction: 0.9,
});
export function deviceLayout(width) {
  const g = DEVICE_GEOMETRY;
  const height = (width * g.heightMm) / g.widthMm;
  const matrixWidth = width * g.matrixWidthFraction;
  const matrixHeight = (matrixWidth * g.rows) / g.columns;
  return {
    width,
    height,
    matrixWidth,
    matrixHeight,
    matrixLeft: (width - matrixWidth) / 2,
    matrixTop: (height - matrixHeight) / 2,
    pixelPitch: matrixWidth / g.columns,
  };
}
