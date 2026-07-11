INSERT INTO app_users (full_name, email, role, password_hash, barangay)
VALUES
  ('PaperBriq Admin', 'admin@paperbriq.local', 'admin', crypt('paperbriq123', gen_salt('bf')), 'Central'),
  ('Barangay Staff Demo', 'staff@paperbriq.local', 'barangay_staff', crypt('paperbriq123', gen_salt('bf')), 'San Isidro'),
  ('Contributor Demo', 'contributor@paperbriq.local', 'contributor', crypt('paperbriq123', gen_salt('bf')), 'San Isidro')
ON CONFLICT (email) DO UPDATE
SET full_name = EXCLUDED.full_name,
    role = EXCLUDED.role,
    barangay = EXCLUDED.barangay,
    updated_at = NOW();

WITH demo_contributors AS (
  INSERT INTO contributors (display_name, phone, barangay)
  VALUES
    ('R. Santos', '+63 917 000 0101', 'San Isidro'),
    ('M. Dela Cruz', '+63 917 000 0102', 'Central'),
    ('K. Reyes', '+63 917 000 0103', 'Mabini')
  RETURNING id, display_name, barangay
)
INSERT INTO collection_events (contributor_id, paper_weight_kg, paper_grade, barangay, status, collected_at)
SELECT id, weight, grade, barangay, status, collected_at
FROM demo_contributors
CROSS JOIN LATERAL (
  VALUES
    (18.50, 'mixed office paper', 'received', NOW() - INTERVAL '2 hours'),
    (26.00, 'newspaper', 'sorted', NOW() - INTERVAL '1 day')
) AS event_data(weight, grade, status, collected_at);

INSERT INTO briquette_batches (batch_code, input_paper_kg, briquettes_produced, moisture_percent, quality_status, produced_at)
VALUES
  ('PBQ-2026-0701-A', 82.00, 410, 8.10, 'ready', NOW() - INTERVAL '3 hours'),
  ('PBQ-2026-0630-B', 64.50, 316, 8.70, 'curing', NOW() - INTERVAL '1 day'),
  ('PBQ-2026-0629-C', 71.25, 355, 7.90, 'ready', NOW() - INTERVAL '2 days')
ON CONFLICT (batch_code) DO NOTHING;
