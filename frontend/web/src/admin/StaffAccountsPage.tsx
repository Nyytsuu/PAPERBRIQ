import React, { useState } from "react";
import { useOutletContext } from "react-router-dom";
import { Field, Panel } from "../components/ui";

export default function StaffAccountsPage(props = {}) {
  const outlet = useOutletContext() || {};
  const { user } = { ...outlet, ...props };
  const [accounts, setAccounts] = useState([]);
  const [message, setMessage] = useState("");
  const [messageType, setMessageType] = useState("error");
  const [isLoading, setLoading] = useState(false);
  const [isCreating, setCreating] = useState(false);
  const [selectedAccount, setSelectedAccount] = useState(null);

  React.useEffect(() => {
    loadAccounts();
  }, []);

  async function loadAccounts() {
    setLoading(true);
    try {
      const response = await fetch(`/api/staff-accounts?creatorRole=${encodeURIComponent(user.role)}`);
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to load staff accounts.");
      setAccounts(result.staffAccounts || []);
      setMessage("");
    } catch (error) {
      setMessageType("error");
      setMessage(error.message);
    } finally {
      setLoading(false);
    }
  }

  async function handleCreate(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const payload = Object.fromEntries(new FormData(form).entries());
    payload.creatorRole = user.role;
    setCreating(true);
    setMessage("");

    try {
      const response = await fetch("/api/staff-accounts", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to create staff account.");
      form.reset();
      setMessageType("success");
      setMessage("Barangay staff account created.");
      await loadAccounts();
    } catch (error) {
      setMessageType("error");
      setMessage(error.message);
    } finally {
      setCreating(false);
    }
  }

  return (
    <section className="split-grid">
      <Panel title="Create Barangay Staff" subtitle="Admin-created staff accounts for local operators.">
        <form className="staff-form" onSubmit={handleCreate}>
          <Field label="Full Name" name="fullName" required />
          <Field label="Email Address" name="email" type="email" required />
          <Field label="Barangay" name="barangay" required />
          <Field label="Temporary Password" name="password" type="password" minLength={8} required />
          <button className="primary-button" type="submit" disabled={isCreating}>
            {isCreating ? "Creating" : "Create Staff Account"}
          </button>
          <p className={`form-message left ${messageType}`}>{message}</p>
        </form>
      </Panel>

      <Panel
        title="Barangay Staff List"
        subtitle="Accounts currently allowed into the web system."
        action={<button className="text-button" type="button" onClick={loadAccounts}>Refresh</button>}
      >
        {isLoading ? (
          <p className="muted-copy">Loading staff accounts...</p>
        ) : (
          <div className="staff-list">
            {accounts.length ? accounts.map((account) => (
              <button
                className="staff-account"
                key={account.id || account.email}
                type="button"
                onClick={() => setSelectedAccount(account)}
              >
                <span className="staff-account-avatar">
                  {account.profileImageUrl ? (
                    <img src={account.profileImageUrl} alt="" aria-hidden="true" />
                  ) : (
                    (account.fullName || account.email || "B").slice(0, 1).toUpperCase()
                  )}
                </span>
                <div>
                  <strong>{account.fullName}</strong>
                  <span>{account.email}</span>
                </div>
                <div>
                  <span>{account.barangay || "Unassigned"}</span>
                  <small>{account.isActive ? "Active" : "Inactive"}</small>
                </div>
              </button>
            )) : <p className="muted-copy">No barangay staff accounts yet.</p>}
          </div>
        )}
      </Panel>

      {selectedAccount && (
        <StaffInfoModal
          account={selectedAccount}
          onClose={() => setSelectedAccount(null)}
        />
      )}
    </section>
  );
}

function StaffInfoModal({ account, onClose }) {
  const initial = (account.fullName || account.email || "B").slice(0, 1).toUpperCase();
  const createdAt = formatAccountDate(account.createdAt);

  return (
    <div className="modal-backdrop" role="presentation" onClick={onClose}>
      <section
        className="staff-info-modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby="staffInfoTitle"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="staff-info-header">
          <span className="staff-info-avatar">
            {account.profileImageUrl ? <img src={account.profileImageUrl} alt="" aria-hidden="true" /> : initial}
          </span>
          <div>
            <p className="eyebrow">Barangay Staff Account</p>
            <h2 id="staffInfoTitle">{account.fullName}</h2>
            <span className={`status-pill ${account.isActive ? "" : "muted"}`}>
              {account.isActive ? "Active" : "Inactive"}
            </span>
          </div>
        </div>

        <div className="staff-info-grid">
          <InfoItem label="Email address" value={account.email} />
          <InfoItem label="Assigned barangay" value={account.barangay || "Unassigned"} />
          <InfoItem label="Role" value={account.roleLabel || "Barangay Staff"} />
          <InfoItem label="Account created" value={createdAt || "Not recorded"} />
          <InfoItem label="Profile image" value={account.profileImageUrl ? "Uploaded" : "Initial avatar only"} />
          <InfoItem label="Access scope" value="Barangay-level tools and reports" />
        </div>

        <div className="staff-info-note">
          <strong>Allowed staff access</strong>
          <span>QR scanning, process tools, local reports, IoT monitoring, and reward claim handling.</span>
        </div>

        <div className="confirm-actions">
          <a className="secondary-button staff-mail-link" href={`mailto:${account.email}`}>
            Email staff
          </a>
          <button className="primary-button confirm-button" type="button" onClick={onClose}>
            Close
          </button>
        </div>
      </section>
    </div>
  );
}

function InfoItem({ label, value }) {
  return (
    <article>
      <span>{label}</span>
      <strong>{value}</strong>
    </article>
  );
}

function formatAccountDate(value) {
  if (!value) return "";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "";
  return new Intl.DateTimeFormat("en-PH", {
    month: "short",
    day: "2-digit",
    year: "numeric"
  }).format(date);
}
