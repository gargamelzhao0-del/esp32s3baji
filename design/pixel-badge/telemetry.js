"use strict";

// The bridge reports percentage USED for both measurements.
// The left instrument on the badge shows quota REMAINING.
function badgeTelemetry(snapshot, simulated = false) {
  const available =
    snapshot?.has_session === true &&
    snapshot.family === "gpt" &&
    ["online", "finished"].includes(snapshot.link);
  function measure(sample, remaining) {
    if (
      !sample ||
      !Number.isFinite(sample.value) ||
      sample.value < 0 ||
      sample.value > 100 ||
      !["estimated", "reported"].includes(sample.quality)
    )
      return { percent: null, text: "--" };
    const percent = Math.round(remaining ? 100 - sample.value : sample.value);
    return {
      percent,
      text: (sample.quality === "estimated" ? "~" : "") + percent + "%",
    };
  }
  const mode = simulated
    ? "SIM"
    : !available
      ? snapshot?.link === "connecting"
        ? "SYNC"
        : "OFF"
      : snapshot.replay
        ? "PLAY"
        : snapshot.link === "finished"
          ? "END"
          : "LIVE";
  const quota = measure(
    simulated
      ? { value: 32, quality: "reported" }
      : available && snapshot.quota,
    true,
  );
  const context = measure(
    simulated
      ? { value: 24, quality: "reported" }
      : available && snapshot.context,
    false,
  );
  return { mode, quota, context };
}
