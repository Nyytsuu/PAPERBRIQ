package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class IotMonitoringScreen implements AppScreen {
    @Override
    public String key() {
        return "iot";
    }

    @Override
    public String navLabel() {
        return "IoT";
    }

    @Override
    public String title() {
        return "IoT Monitoring";
    }

    @Override
    public String subtitle() {
        return "Real-time sensor data and device status.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        root.addView(ui.sectionTitle("Real-time Sensor Data", "Live"));
        ui.addGrid(root, new android.view.View[]{
            ui.sensorCard("Temperature", "29.4 C", "Normal", AppColors.GREEN),
            ui.sensorCard("Humidity", "65 %RH", "Normal", AppColors.GREEN),
            ui.sensorCard("Rainwater Level", "78 %", "Normal", AppColors.BLUE),
            ui.sensorCard("Battery Level", "92 %", "Good", AppColors.GREEN),
            ui.sensorCard("Power Output", "240 w", "Normal", AppColors.BLUE),
            ui.sensorCard("Water Flow Rate", "18.6 L/min", "Normal", AppColors.BLUE),
            ui.sensorCard("System Pressure", "2.3 bar", "Normal", AppColors.GREEN),
            ui.sensorCard("TDS Water Quality", "152 ppm", "Good", AppColors.BLUE)
        }, 2);

        root.addView(ui.sectionTitle("Device Status", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.gaugeCard("Motor Status", "ON", "Running"),
            ui.gaugeCard("Pump Status", "ON", "Running"),
            ui.gaugeCard("Heater Status", "OFF", "Standby"),
            ui.gaugeCard("Fan Status", "ON", "Running"),
            ui.gaugeCard("Safety Interlock", "LOCKED", "Secure")
        }, 2);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("System Alerts", new String[][]{
                {"Low Rainwater Level", "Warning"},
                {"High Motor Temperature", "Warning"},
                {"Scheduled Maintenance", "Info"},
                {"System Check OK", "Info"}
            }),
            ui.infoListCard("Diagnostics", new String[][]{
                {"Sensor Connectivity", "OK"},
                {"Data Transmission", "OK"},
                {"Edge Controller", "OK"},
                {"Cloud Sync", "OK"},
                {"Power Supply", "OK"}
            })
        }, 1);

        root.addView(ui.sectionTitle("Network Health", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Online Devices", "12 / 12", "All connected", AppColors.GREEN),
            ui.statCard("Packet Success", "99.6%", "Excellent", AppColors.GREEN),
            ui.statCard("Response Time", "320 ms", "Good", AppColors.GREEN),
            ui.statCard("Last Data Sync", "10:35 AM", "Just now", AppColors.GREEN)
        }, 2);
    }
}
