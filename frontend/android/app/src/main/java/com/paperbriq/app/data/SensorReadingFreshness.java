package com.paperbriq.app.data;

import java.time.Instant;

/** Checks telemetry age using server timestamps and elapsed local time. */
public final class SensorReadingFreshness {
    private SensorReadingFreshness() {}

    public static boolean isFresh(String generatedAt, String updatedAt, long receivedAt,
                                  long now, String source, String status, double value,
                                  double minimum, double maximum) {
        if (!"esp32".equalsIgnoreCase(source) || !Double.isFinite(value)
            || value < minimum || value > maximum || receivedAt < 0 || now < receivedAt) return false;
        if (!"Normal".equalsIgnoreCase(status) && !"Warning".equalsIgnoreCase(status)) return false;
        try {
            long serverAge = Instant.parse(generatedAt).toEpochMilli() - Instant.parse(updatedAt).toEpochMilli();
            long localAge = now - receivedAt;
            return serverAge >= 0 && serverAge < 30000 && localAge < 30000 - serverAge;
        } catch (Exception ignored) { return false; }
    }
}
