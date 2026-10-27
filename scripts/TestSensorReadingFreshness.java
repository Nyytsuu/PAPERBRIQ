import com.paperbriq.app.data.SensorReadingFreshness;

public class TestSensorReadingFreshness {
    private static boolean check(String sample, long elapsed, String source, String status, double value) {
        return SensorReadingFreshness.isFresh("2026-10-25T17:19:56.099Z", sample,
            1000, 1000 + elapsed, source, status, value, -40, 80);
    }
    public static void main(String[] args) {
        // Fresh timestamps/value observed in the deployed API response.
        String sample = "2026-10-25T17:19:51.447Z";
        assert check(sample, 0, "esp32", "Normal", 32.3);
        assert check(sample, 20000, "esp32", "Warning", 32.3);
        assert !check(sample, 26000, "esp32", "Normal", 32.3);
        assert !check("2026-10-25T17:18:00Z", 0, "esp32", "Normal", 32.3);
        assert !check("2026-10-25T17:20:00Z", 0, "esp32", "Normal", 32.3);
        assert !check("Never", 0, "esp32", "Normal", 32.3);
        assert !check(sample, 0, "demo", "Normal", 32.3);
        assert !check(sample, 0, "esp32", "Offline", 32.3);
        assert !check(sample, 0, "esp32", "Normal", Double.NaN);
        assert !check(sample, 0, "esp32", "Normal", 100);
        assert !check(sample, -1, "esp32", "Normal", 32.3);
        assert !SensorReadingFreshness.isFresh("2026-10-25T17:19:56.099Z", sample,
            -1, 1000, "esp32", "Normal", 32.3, -40, 80);
        System.out.println("Sensor display freshness: 12 cases passed");
    }
}
