-- PaperBriq MySQL Workbench schema.
-- Import this file in MySQL Workbench, not database/postgres/schema.sql.

CREATE DATABASE IF NOT EXISTS paperbriq
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE paperbriq;

CREATE TABLE IF NOT EXISTS app_users (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  full_name VARCHAR(160) NOT NULL,
  email VARCHAR(180) NOT NULL UNIQUE,
  role ENUM('admin', 'barangay_staff', 'contributor') NOT NULL,
  password_hash CHAR(64) NOT NULL,
  barangay VARCHAR(120),
  is_active BOOLEAN NOT NULL DEFAULT TRUE,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX idx_app_users_role (role)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS contributors (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  user_id CHAR(36),
  display_name VARCHAR(160) NOT NULL,
  phone VARCHAR(40),
  barangay VARCHAR(120) NOT NULL,
  joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_contributor_display_barangay (display_name, barangay),
  CONSTRAINT fk_contributors_user
    FOREIGN KEY (user_id) REFERENCES app_users(id)
    ON DELETE SET NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS collection_events (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  contributor_id CHAR(36),
  collected_by CHAR(36),
  paper_weight_kg DECIMAL(10, 2) NOT NULL,
  paper_grade VARCHAR(120) NOT NULL DEFAULT 'mixed office paper',
  barangay VARCHAR(120) NOT NULL,
  status VARCHAR(40) NOT NULL DEFAULT 'received',
  collected_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_collection_events_barangay (barangay),
  INDEX idx_collection_events_collected_at (collected_at DESC),
  CONSTRAINT chk_collection_weight CHECK (paper_weight_kg > 0),
  CONSTRAINT fk_collection_contributor
    FOREIGN KEY (contributor_id) REFERENCES contributors(id)
    ON DELETE SET NULL,
  CONSTRAINT fk_collection_user
    FOREIGN KEY (collected_by) REFERENCES app_users(id)
    ON DELETE SET NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS briquette_batches (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  batch_code VARCHAR(60) NOT NULL UNIQUE,
  input_paper_kg DECIMAL(10, 2) NOT NULL,
  briquettes_produced INT NOT NULL,
  moisture_percent DECIMAL(5, 2) NOT NULL DEFAULT 8.50,
  quality_status VARCHAR(40) NOT NULL DEFAULT 'ready',
  produced_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_briquette_batches_produced_at (produced_at DESC),
  CONSTRAINT chk_batch_input CHECK (input_paper_kg > 0),
  CONSTRAINT chk_batch_output CHECK (briquettes_produced >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS access_requests (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  full_name VARCHAR(160) NOT NULL,
  email VARCHAR(180) NOT NULL,
  requested_role ENUM('admin', 'barangay_staff', 'contributor') NOT NULL,
  barangay VARCHAR(120),
  message TEXT,
  status VARCHAR(40) NOT NULL DEFAULT 'pending',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS audit_log (
  id BIGINT PRIMARY KEY AUTO_INCREMENT,
  actor_id CHAR(36),
  action VARCHAR(160) NOT NULL,
  metadata JSON NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_audit_actor
    FOREIGN KEY (actor_id) REFERENCES app_users(id)
    ON DELETE SET NULL
) ENGINE=InnoDB;

CREATE OR REPLACE VIEW dashboard_totals AS
SELECT
  CAST(COALESCE((SELECT SUM(paper_weight_kg) FROM collection_events), 0) AS DECIMAL(10, 2)) AS paper_kg,
  CAST(COALESCE((SELECT SUM(briquettes_produced) FROM briquette_batches), 0) AS SIGNED) AS briquettes,
  CAST(COALESCE((SELECT COUNT(*) FROM contributors), 0) AS SIGNED) AS contributors,
  CAST(COALESCE((SELECT ROUND(SUM(input_paper_kg) * 1.72, 2) FROM briquette_batches), 0) AS DECIMAL(10, 2)) AS co2_kg_avoided;

INSERT INTO app_users (full_name, email, role, password_hash, barangay)
VALUES
  ('PaperBriq Admin', 'admin@paperbriq.local', 'admin', SHA2('paperbriq123', 256), 'Central'),
  ('Barangay Staff Demo', 'staff@paperbriq.local', 'barangay_staff', SHA2('paperbriq123', 256), 'San Isidro'),
  ('Contributor Demo', 'contributor@paperbriq.local', 'contributor', SHA2('paperbriq123', 256), 'San Isidro')
ON DUPLICATE KEY UPDATE
  full_name = VALUES(full_name),
  role = VALUES(role),
  password_hash = VALUES(password_hash),
  barangay = VALUES(barangay),
  updated_at = CURRENT_TIMESTAMP;

INSERT INTO contributors (display_name, phone, barangay)
SELECT seed.display_name, seed.phone, seed.barangay
FROM (
  SELECT 'R. Santos' AS display_name, '+63 917 000 0101' AS phone, 'San Isidro' AS barangay
  UNION ALL SELECT 'M. Dela Cruz', '+63 917 000 0102', 'Central'
  UNION ALL SELECT 'K. Reyes', '+63 917 000 0103', 'Mabini'
) AS seed
WHERE NOT EXISTS (SELECT 1 FROM contributors);

INSERT INTO collection_events (contributor_id, paper_weight_kg, paper_grade, barangay, status, collected_at)
SELECT c.id, seed.paper_weight_kg, seed.paper_grade, c.barangay, seed.status, seed.collected_at
FROM contributors c
JOIN (
  SELECT 'R. Santos' AS display_name, 18.50 AS paper_weight_kg, 'mixed office paper' AS paper_grade, 'received' AS status, NOW() - INTERVAL 2 HOUR AS collected_at
  UNION ALL SELECT 'M. Dela Cruz', 26.00, 'newspaper', 'sorted', NOW() - INTERVAL 1 DAY
  UNION ALL SELECT 'K. Reyes', 12.75, 'carton and office paper', 'queued', NOW() - INTERVAL 2 DAY
) AS seed ON seed.display_name = c.display_name
WHERE NOT EXISTS (SELECT 1 FROM collection_events);

INSERT INTO briquette_batches (batch_code, input_paper_kg, briquettes_produced, moisture_percent, quality_status, produced_at)
VALUES
  ('PBQ-2026-0701-A', 82.00, 410, 8.10, 'ready', NOW() - INTERVAL 3 HOUR),
  ('PBQ-2026-0630-B', 64.50, 316, 8.70, 'curing', NOW() - INTERVAL 1 DAY),
  ('PBQ-2026-0629-C', 71.25, 355, 7.90, 'ready', NOW() - INTERVAL 2 DAY)
ON DUPLICATE KEY UPDATE
  input_paper_kg = VALUES(input_paper_kg),
  briquettes_produced = VALUES(briquettes_produced),
  moisture_percent = VALUES(moisture_percent),
  quality_status = VALUES(quality_status),
  produced_at = VALUES(produced_at);

CREATE TABLE IF NOT EXISTS process_runs (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  batch_code VARCHAR(60) NOT NULL,
  status VARCHAR(40) NOT NULL DEFAULT 'in_progress',
  overall_progress DECIMAL(5, 2) NOT NULL DEFAULT 0,
  estimated_time_remaining VARCHAR(80),
  target_briquettes INT NOT NULL DEFAULT 0,
  completed_briquettes INT NOT NULL DEFAULT 0,
  started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  completed_at TIMESTAMP NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS process_steps (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  step_name VARCHAR(120) NOT NULL UNIQUE,
  progress_percent DECIMAL(5, 2) NOT NULL DEFAULT 0,
  status VARCHAR(40) NOT NULL,
  detail VARCHAR(240),
  display_order INT NOT NULL DEFAULT 0
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS process_logs (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  event VARCHAR(160) NOT NULL,
  status VARCHAR(60) NOT NULL,
  log_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_process_logs_time (log_time DESC)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS quality_checks (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  check_label VARCHAR(120) NOT NULL UNIQUE,
  check_value VARCHAR(160) NOT NULL,
  display_order INT NOT NULL DEFAULT 0
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS process_actions (
  id CHAR(36) PRIMARY KEY,
  action VARCHAR(80) NOT NULL,
  payload JSON NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS iot_sensor_readings (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  sensor_name VARCHAR(120) NOT NULL,
  reading_value DECIMAL(10, 2) NOT NULL,
  reading_unit VARCHAR(40) NOT NULL,
  status VARCHAR(40) NOT NULL,
  recorded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_iot_sensor_latest (sensor_name, recorded_at DESC)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS device_statuses (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  device_name VARCHAR(120) NOT NULL UNIQUE,
  device_value VARCHAR(80) NOT NULL,
  status VARCHAR(80) NOT NULL,
  display_order INT NOT NULL DEFAULT 0,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS system_alerts (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  title VARCHAR(160) NOT NULL,
  severity VARCHAR(40) NOT NULL,
  message VARCHAR(255) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_system_alerts_created (created_at DESC)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS diagnostics (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  item VARCHAR(120) NOT NULL UNIQUE,
  status VARCHAR(40) NOT NULL,
  display_order INT NOT NULL DEFAULT 0
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS point_transactions (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  contributor_email VARCHAR(180),
  points INT NOT NULL,
  transaction_type VARCHAR(40) NOT NULL,
  title VARCHAR(180) NOT NULL,
  happened_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_point_transactions_happened (happened_at DESC)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS rewards (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  reward_code VARCHAR(80) NOT NULL UNIQUE,
  reward_name VARCHAR(160) NOT NULL,
  points_cost INT NOT NULL,
  category VARCHAR(80) NOT NULL,
  is_active BOOLEAN NOT NULL DEFAULT TRUE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS reward_redemptions (
  id CHAR(36) PRIMARY KEY,
  reward_code VARCHAR(80) NOT NULL,
  contributor_email VARCHAR(180),
  points_spent INT NOT NULL,
  status VARCHAR(40) NOT NULL DEFAULT 'redeemed',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS system_events (
  id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
  title VARCHAR(160) NOT NULL,
  description VARCHAR(255) NOT NULL,
  category VARCHAR(80) NOT NULL,
  event_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_system_events_time (event_time DESC)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS app_settings (
  settings_key VARCHAR(80) PRIMARY KEY,
  settings_json JSON NOT NULL,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS qr_scans (
  id CHAR(36) PRIMARY KEY,
  qr_code VARCHAR(160) NOT NULL,
  contributor_name VARCHAR(160) NOT NULL,
  barangay VARCHAR(120) NOT NULL,
  weight_kg DECIMAL(10, 2) NOT NULL DEFAULT 0,
  points_earned INT NOT NULL DEFAULT 0,
  status VARCHAR(40) NOT NULL DEFAULT 'Success',
  scan_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_qr_scans_time (scan_time DESC)
) ENGINE=InnoDB;

INSERT INTO process_runs (batch_code, status, overall_progress, estimated_time_remaining, target_briquettes, completed_briquettes, started_at)
SELECT 'PBQ-2026-0701-A', 'in_progress', 65, '1 hr 20 mins', 20, 8, NOW() - INTERVAL 3 HOUR
WHERE NOT EXISTS (SELECT 1 FROM process_runs);

INSERT INTO process_steps (step_name, progress_percent, status, detail, display_order)
VALUES
  ('Collect Paper', 100, 'completed', '45 kg / 60 kg', 1),
  ('Shred Paper', 78, 'in_progress', 'Shredding at 78%', 2),
  ('Pulp and Mix', 60, 'in_progress', 'Hydration at 60%', 3),
  ('Press and Mold', 0, 'not_started', '0 / 20 briquettes', 4),
  ('Drying', 0, 'not_started', 'Waiting to start', 5)
ON DUPLICATE KEY UPDATE
  progress_percent = VALUES(progress_percent),
  status = VALUES(status),
  detail = VALUES(detail),
  display_order = VALUES(display_order);

INSERT INTO process_logs (event, status, log_time)
SELECT seed.event, seed.status, seed.log_time
FROM (
  SELECT 'Shredding Paper Started' AS event, 'Process' AS status, NOW() - INTERVAL 90 MINUTE AS log_time
  UNION ALL SELECT 'Rainwater Added', 'Resource', NOW() - INTERVAL 75 MINUTE
  UNION ALL SELECT 'Pulping Mixture Began', 'Process', NOW() - INTERVAL 50 MINUTE
  UNION ALL SELECT 'Paper Collection Completed', 'Completed', NOW() - INTERVAL 35 MINUTE
  UNION ALL SELECT 'System Check OK', 'System', NOW() - INTERVAL 10 MINUTE
) seed
WHERE NOT EXISTS (SELECT 1 FROM process_logs);

INSERT INTO quality_checks (check_label, check_value, display_order)
VALUES
  ('Moisture', '8.5% optimal', 1),
  ('Mold Pressure', '2.3 bar', 2),
  ('Drying Temperature', '55 C', 3),
  ('Safety Interlock', 'Locked', 4)
ON DUPLICATE KEY UPDATE
  check_value = VALUES(check_value),
  display_order = VALUES(display_order);

INSERT INTO iot_sensor_readings (sensor_name, reading_value, reading_unit, status, recorded_at)
SELECT seed.sensor_name, seed.reading_value, seed.reading_unit, seed.status, NOW()
FROM (
  SELECT 'Temperature' AS sensor_name, 29.4 AS reading_value, 'C' AS reading_unit, 'Normal' AS status
  UNION ALL SELECT 'Humidity', 65, '%RH', 'Normal'
  UNION ALL SELECT 'Rainwater Level', 78, '%', 'Normal'
  UNION ALL SELECT 'Battery Level', 92, '%', 'Good'
  UNION ALL SELECT 'Power Output', 240, 'w', 'Normal'
  UNION ALL SELECT 'Water Flow Rate', 18.6, 'L/min', 'Normal'
  UNION ALL SELECT 'System Pressure', 2.3, 'bar', 'Normal'
  UNION ALL SELECT 'TDS Water Quality', 152, 'ppm', 'Good'
) seed
WHERE NOT EXISTS (SELECT 1 FROM iot_sensor_readings);

INSERT INTO device_statuses (device_name, device_value, status, display_order)
VALUES
  ('Motor Status', 'ON', 'Running', 1),
  ('Pump Status', 'ON', 'Running', 2),
  ('Heater Status', 'OFF', 'Standby', 3),
  ('Fan Status', 'ON', 'Running', 4),
  ('Safety Interlock', 'LOCKED', 'Secure', 5)
ON DUPLICATE KEY UPDATE
  device_value = VALUES(device_value),
  status = VALUES(status),
  display_order = VALUES(display_order);

INSERT INTO system_alerts (title, severity, message, created_at)
SELECT seed.title, seed.severity, seed.message, seed.created_at
FROM (
  SELECT 'Low Rainwater Level' AS title, 'Warning' AS severity, 'Rainwater level is below 80% threshold.' AS message, NOW() - INTERVAL 7 MINUTE AS created_at
  UNION ALL SELECT 'High Motor Temperature', 'Warning', 'Motor temperature is above normal range.', NOW() - INTERVAL 12 MINUTE
  UNION ALL SELECT 'Scheduled Maintenance', 'Info', 'Filter cleaning scheduled today at 2:00 PM.', NOW() - INTERVAL 80 MINUTE
  UNION ALL SELECT 'System Check OK', 'Info', 'All systems operating within normal parameters.', NOW() - INTERVAL 15 MINUTE
) seed
WHERE NOT EXISTS (SELECT 1 FROM system_alerts);

INSERT INTO diagnostics (item, status, display_order)
VALUES
  ('Sensor Connectivity', 'OK', 1),
  ('Data Transmission', 'OK', 2),
  ('Edge Controller', 'OK', 3),
  ('Cloud Sync', 'OK', 4),
  ('Power Supply', 'OK', 5),
  ('Backup Power', 'OK', 6)
ON DUPLICATE KEY UPDATE
  status = VALUES(status),
  display_order = VALUES(display_order);

INSERT INTO rewards (reward_code, reward_name, points_cost, category)
VALUES
  ('voucher-100', 'Eco Store Voucher P100', 100, 'Vouchers'),
  ('voucher-250', 'Eco Store Voucher P250', 250, 'Vouchers'),
  ('tote-bag', 'Eco Tote Bag', 300, 'Eco Products'),
  ('plant-tree', 'Plant a Tree', 350, 'Green Donations'),
  ('water-bottle', 'Eco Water Bottle', 400, 'Eco Products')
ON DUPLICATE KEY UPDATE
  reward_name = VALUES(reward_name),
  points_cost = VALUES(points_cost),
  category = VALUES(category),
  is_active = TRUE;

INSERT INTO point_transactions (contributor_email, points, transaction_type, title, happened_at)
SELECT seed.email, seed.points, seed.transaction_type, seed.title, seed.happened_at
FROM (
  SELECT 'contributor@paperbriq.local' AS email, 45 AS points, 'earned' AS transaction_type, 'Paper Collection' AS title, NOW() - INTERVAL 1 DAY AS happened_at
  UNION ALL SELECT 'contributor@paperbriq.local', 30, 'earned', 'Pulping Mixture Contribution', NOW() - INTERVAL 3 DAY
  UNION ALL SELECT 'contributor@paperbriq.local', 25, 'earned', 'System Check Bonus', NOW() - INTERVAL 5 DAY
  UNION ALL SELECT 'contributor@paperbriq.local', -60, 'redeemed', 'Eco Store Voucher P100', NOW() - INTERVAL 6 DAY
) seed
WHERE NOT EXISTS (SELECT 1 FROM point_transactions);

INSERT INTO system_events (title, description, category, event_time)
SELECT seed.title, seed.description, seed.category, seed.event_time
FROM (
  SELECT 'System Check OK' AS title, 'All systems are operating normally.' AS description, 'System' AS category, NOW() - INTERVAL 10 MINUTE AS event_time
  UNION ALL SELECT 'Paper Collection Completed', 'Collected 45 kg / 60 kg.', 'Process Run', NOW() - INTERVAL 35 MINUTE
  UNION ALL SELECT 'Pulping Mixture Began', 'Pulping mixture process started.', 'Process Run', NOW() - INTERVAL 50 MINUTE
  UNION ALL SELECT 'Rainwater Added', 'Added 120 L of rainwater.', 'Resource', NOW() - INTERVAL 75 MINUTE
  UNION ALL SELECT 'QR Code Scanned', 'User: Juan Dela Cruz. Points earned: +20 pts.', 'QR Scan', NOW() - INTERVAL 2 HOUR
) seed
WHERE NOT EXISTS (SELECT 1 FROM system_events);

INSERT INTO app_settings (settings_key, settings_json)
VALUES (
  'default',
  JSON_OBJECT(
    'profile', JSON_OBJECT('fullName', 'Juan Dela Cruz', 'email', 'juan.delacruz@example.com', 'phone', '+63 912 345 6789', 'role', 'Contributor'),
    'account', JSON_OBJECT('username', 'juan.delacruz', 'accountType', 'Contributor', 'memberSince', 'May 15, 2024', 'timeZone', '(GMT+8) Asia/Manila'),
    'machine', JSON_OBJECT('defaultBriquetteSize', 'Standard (42x22 cm)', 'autoStartMachine', true, 'shredLevel', 'Medium', 'dryingTemperature', '55 C'),
    'notifications', JSON_OBJECT('email', true, 'sms', true, 'processAlerts', true, 'maintenanceReminders', true, 'weeklySummary', true),
    'qr', JSON_OBJECT('display', true, 'autoRefresh', false, 'expiry', '24 Hours', 'allowDownload', true),
    'appearance', JSON_OBJECT('theme', 'Light', 'language', 'English (US)', 'dateFormat', 'MM/DD/YYYY')
  )
)
ON DUPLICATE KEY UPDATE settings_json = VALUES(settings_json);

INSERT INTO qr_scans (id, qr_code, contributor_name, barangay, weight_kg, points_earned, status, scan_time)
SELECT UUID(), seed.qr_code, seed.contributor_name, seed.barangay, seed.weight_kg, seed.points_earned, 'Success', seed.scan_time
FROM (
  SELECT 'PBQ-MARIA-001' AS qr_code, 'Maria Santos' AS contributor_name, 'San Isidro' AS barangay, 12.50 AS weight_kg, 15 AS points_earned, NOW() - INTERVAL 20 MINUTE AS scan_time
  UNION ALL SELECT 'PBQ-PEDRO-002', 'Pedro Dela Cruz', 'San Rafael', 8.00, 10, NOW() - INTERVAL 35 MINUTE
  UNION ALL SELECT 'PBQ-ANA-003', 'Ana Reyes', 'San Isidro', 15.00, 18, NOW() - INTERVAL 50 MINUTE
  UNION ALL SELECT 'PBQ-LUIS-004', 'Luis Gonzales', 'San Roque', 6.00, 7, NOW() - INTERVAL 65 MINUTE
  UNION ALL SELECT 'PBQ-CARMEN-005', 'Carmen Villanueva', 'San Isidro', 10.00, 12, NOW() - INTERVAL 80 MINUTE
) seed
WHERE NOT EXISTS (SELECT 1 FROM qr_scans);
