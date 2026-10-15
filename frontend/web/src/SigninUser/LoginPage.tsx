import React, { useState } from "react";

export default function LoginPage({ onLogin }) {
  const [role, setRole] = useState("admin");
  const [email, setEmail] = useState("admin@paperbriq.local");
  const [password, setPassword] = useState("");
  const [showPassword, setShowPassword] = useState(false);
  const [keepLoggedIn, setKeepLoggedIn] = useState(false);
  const [message, setMessage] = useState("");
  const [isSubmitting, setSubmitting] = useState(false);
  const [forgotOpen, setForgotOpen] = useState(false);
  const [resetEmail, setResetEmail] = useState("admin@paperbriq.local");
  const [resetMessage, setResetMessage] = useState("");
  const [isResetSubmitting, setResetSubmitting] = useState(false);

  async function handleSubmit(event) {
    event.preventDefault();
    setMessage("");
    setSubmitting(true);

    try {
      const response = await fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ email: email.trim(), password, role })
      });
      const result = await response.json();

      if (!response.ok) {
        throw new Error(result.error || "Unable to sign in.");
      }

      if (!["admin", "barangay_staff"].includes(result.user.role)) {
        throw new Error("Website access is for Admin and Barangay Staff only.");
      }

      onLogin(result.user, result.summary, keepLoggedIn);
    } catch (error) {
      setMessage(error.message);
    } finally {
      setSubmitting(false);
    }
  }

  function handleRoleChange(nextRole) {
    setRole(nextRole);
    const nextEmail = nextRole === "admin" ? "admin@paperbriq.local" : "staff@paperbriq.local";
    setEmail(nextEmail);
    setResetEmail(nextEmail);
    setResetMessage("");
  }

  const roleHint = role === "admin" ? "Admin selected" : "Barangay Staff selected";

  async function handleForgotPassword() {
    setResetMessage("");
    setResetSubmitting(true);

    try {
      const response = await fetch("/api/auth/forgot-password", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ email: resetEmail.trim(), role })
      });
      const result = await response.json();

      if (!response.ok) {
        if (response.status === 404 && result.error === "API route not found.") {
          setResetMessage("Reset request noted. Please contact the system admin if no email arrives.");
          return;
        }
        throw new Error(result.error || "Unable to request password reset.");
      }

      setResetMessage(result.message || "Password reset request sent.");
    } catch (error) {
      setResetMessage(error.message);
    } finally {
      setResetSubmitting(false);
    }
  }

  return (
    <main className="web-system login-page" aria-label="PaperBriq monitoring system">
      <section className="auth-screen">
        <div className="auth-visual">
          <img src="/assets/images/paperbriq-illustration.png" alt="" />
          <div className="brand-panel">
            <img src="/assets/images/paperbriq-logo-lockup.svg" alt="PaperBriq" />
            <h1>Monitoring System</h1>
            <p>Live visibility into paper briquette production, machine health, and environmental impact.</p>
          </div>
          <div className="auth-metrics" aria-label="System highlights">
            <MetricMini label="Machine" value="Live" />
            <MetricMini label="Reports" value="Ready" />
            <MetricMini label="Sensors" value="Online" />
          </div>
        </div>

        <form className="login-panel" onSubmit={handleSubmit}>
          <div className="login-heading">
            <img src="/assets/images/paperbriq-logo-mark.svg" alt="" aria-hidden="true" />
            <div>
              <p className="eyebrow">PaperBriq Monitor</p>
              <h1>Welcome back</h1>
              <p>Sign in to view live telemetry, alerts, and performance history.</p>
            </div>
          </div>

          <fieldset className="role-selector">
            <legend>Web Role</legend>
            {[
              ["admin", "Admin"],
              ["barangay_staff", "Barangay Staff"]
            ].map(([value, label]) => (
              <label key={value}>
                <input
                  type="radio"
                  name="role"
                  value={value}
                  checked={role === value}
                  onChange={() => handleRoleChange(value)}
                />
                <span>{label}</span>
              </label>
            ))}
          </fieldset>
          <p className="role-note">{roleHint}</p>

          <label className="field-group">
            <span>Email Address</span>
            <span className="input-shell">
              <input
                type="email"
                autoComplete="email"
                placeholder="admin@paperbriq.local"
                value={email}
                onChange={(event) => {
                  setEmail(event.target.value);
                  setResetEmail(event.target.value);
                }}
                required
              />
            </span>
          </label>

          <label className="field-group">
            <span>Password</span>
            <span className="input-shell">
              <input
                type={showPassword ? "text" : "password"}
                autoComplete="current-password"
                placeholder="Enter password"
                value={password}
                onChange={(event) => setPassword(event.target.value)}
                required
              />
              <button className="icon-button" type="button" onClick={() => setShowPassword(!showPassword)}>
                {showPassword ? "Hide" : "Show"}
              </button>
            </span>
          </label>

          <label className="keep-login">
            <input
              type="checkbox"
              checked={keepLoggedIn}
              onChange={(event) => setKeepLoggedIn(event.target.checked)}
            />
            <span>Keep me logged in</span>
          </label>

          <button className="primary-button" type="submit" disabled={isSubmitting}>
            {isSubmitting ? "Signing in" : "Sign in"}
          </button>
          <p className="forgot-copy">
            Forgot password?
            <button
              type="button"
              onClick={() => {
                setForgotOpen(true);
                setResetMessage("");
              }}
            >
              click here
            </button>
          </p>
          <p className="form-message" role="status" aria-live="polite">{message}</p>
        </form>

        {forgotOpen && (
          <div className="modal-backdrop" role="presentation" onClick={() => setForgotOpen(false)}>
            <section className="reset-modal" role="dialog" aria-modal="true" aria-labelledby="resetTitle" onClick={(event) => event.stopPropagation()}>
              <h2 id="resetTitle">Forgot password?</h2>
              <p>Enter your web account email and we will prepare reset instructions. Gmail delivery can be connected later.</p>
              <label className="field-group">
                <span>Email Address</span>
                <span className="input-shell">
                  <input
                    type="email"
                    value={resetEmail}
                    onChange={(event) => setResetEmail(event.target.value)}
                    placeholder="account email"
                  />
                </span>
              </label>
              <div className="reset-actions">
                <button className="secondary-button" type="button" onClick={() => setForgotOpen(false)}>
                  Cancel
                </button>
                <button className="primary-button reset-button" type="button" disabled={isResetSubmitting} onClick={handleForgotPassword}>
                  {isResetSubmitting ? "Sending" : "Send reset"}
                </button>
              </div>
              <p className="reset-message" role="status" aria-live="polite">{resetMessage}</p>
            </section>
          </div>
        )}
      </section>
    </main>
  );
}

function MetricMini({ label, value }) {
  return (
    <article>
      <span>{label}</span>
      <strong>{value}</strong>
    </article>
  );
}
