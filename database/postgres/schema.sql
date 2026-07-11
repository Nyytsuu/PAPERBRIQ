-- PaperBriq PostgreSQL schema.
-- Import this with PostgreSQL tools such as pgAdmin or psql, not mysql.exe/MySQL Workbench.

CREATE EXTENSION IF NOT EXISTS pgcrypto;

DO $$
BEGIN
  CREATE TYPE user_role AS ENUM ('admin', 'barangay_staff', 'contributor');
EXCEPTION
  WHEN duplicate_object THEN NULL;
END
$$;

CREATE TABLE IF NOT EXISTS app_users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  full_name TEXT NOT NULL,
  email TEXT NOT NULL UNIQUE,
  role user_role NOT NULL,
  password_hash TEXT NOT NULL,
  barangay TEXT,
  is_active BOOLEAN NOT NULL DEFAULT TRUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS contributors (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES app_users(id) ON DELETE SET NULL,
  display_name TEXT NOT NULL,
  phone TEXT,
  barangay TEXT NOT NULL,
  joined_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS collection_events (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  contributor_id UUID REFERENCES contributors(id) ON DELETE SET NULL,
  collected_by UUID REFERENCES app_users(id) ON DELETE SET NULL,
  paper_weight_kg NUMERIC(10, 2) NOT NULL CHECK (paper_weight_kg > 0),
  paper_grade TEXT NOT NULL DEFAULT 'mixed office paper',
  barangay TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'received',
  collected_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS briquette_batches (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  batch_code TEXT NOT NULL UNIQUE,
  input_paper_kg NUMERIC(10, 2) NOT NULL CHECK (input_paper_kg > 0),
  briquettes_produced INTEGER NOT NULL CHECK (briquettes_produced >= 0),
  moisture_percent NUMERIC(5, 2) NOT NULL DEFAULT 8.5,
  quality_status TEXT NOT NULL DEFAULT 'ready',
  produced_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS access_requests (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  full_name TEXT NOT NULL,
  email TEXT NOT NULL,
  requested_role user_role NOT NULL,
  barangay TEXT,
  message TEXT,
  status TEXT NOT NULL DEFAULT 'pending',
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS audit_log (
  id BIGSERIAL PRIMARY KEY,
  actor_id UUID REFERENCES app_users(id) ON DELETE SET NULL,
  action TEXT NOT NULL,
  metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_app_users_role ON app_users(role);
CREATE INDEX IF NOT EXISTS idx_collection_events_barangay ON collection_events(barangay);
CREATE INDEX IF NOT EXISTS idx_collection_events_collected_at ON collection_events(collected_at DESC);
CREATE INDEX IF NOT EXISTS idx_briquette_batches_produced_at ON briquette_batches(produced_at DESC);

CREATE OR REPLACE VIEW dashboard_totals AS
SELECT
  COALESCE((SELECT SUM(paper_weight_kg) FROM collection_events), 0)::NUMERIC(10, 2) AS paper_kg,
  COALESCE((SELECT SUM(briquettes_produced) FROM briquette_batches), 0)::INTEGER AS briquettes,
  COALESCE((SELECT COUNT(*) FROM contributors), 0)::INTEGER AS contributors,
  COALESCE((SELECT ROUND(SUM(input_paper_kg) * 1.72, 2) FROM briquette_batches), 0)::NUMERIC(10, 2) AS co2_kg_avoided;
