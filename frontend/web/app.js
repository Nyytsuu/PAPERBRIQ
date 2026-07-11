const loginForm = document.querySelector("#loginForm");
const authScreen = document.querySelector("#authScreen");
const dashboardScreen = document.querySelector("#dashboardScreen");
const formMessage = document.querySelector("#formMessage");
const togglePassword = document.querySelector("#togglePassword");
const passwordInput = document.querySelector("#password");
const googleButton = document.querySelector("#googleButton");
const requestAccessButton = document.querySelector("#requestAccessButton");
const accessDialog = document.querySelector("#accessDialog");
const accessForm = document.querySelector("#accessForm");
const logoutButton = document.querySelector("#logoutButton");

const roleLabels = {
  admin: "Admin",
  barangay_staff: "Barangay Staff",
  contributor: "Contributor"
};

togglePassword.addEventListener("click", () => {
  const isHidden = passwordInput.type === "password";
  passwordInput.type = isHidden ? "text" : "password";
  togglePassword.setAttribute("aria-label", isHidden ? "Hide password" : "Show password");
});

googleButton.addEventListener("click", () => {
  showMessage("Google sign-in is ready for an OAuth provider connection.", "neutral");
});

requestAccessButton.addEventListener("click", () => {
  if (typeof accessDialog.showModal === "function") {
    accessDialog.showModal();
  }
});

logoutButton.addEventListener("click", () => {
  dashboardScreen.hidden = true;
  authScreen.hidden = false;
  loginForm.reset();
  loginForm.querySelector("input[value='admin']").checked = true;
});

loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  showMessage("");

  const formData = new FormData(loginForm);
  const payload = {
    email: formData.get("email"),
    password: formData.get("password"),
    role: formData.get("role")
  };

  const submitButton = loginForm.querySelector("button[type='submit']");
  submitButton.disabled = true;
  submitButton.textContent = "Signing in";

  try {
    const response = await fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const result = await response.json();

    if (!response.ok) {
      throw new Error(result.error || "Unable to sign in.");
    }

    openDashboard(result.user, result.summary);
  } catch (error) {
    showMessage(error.message);
  } finally {
    submitButton.disabled = false;
    submitButton.innerHTML = `
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4"></path>
        <path d="m10 17 5-5-5-5"></path>
        <path d="M15 12H3"></path>
      </svg>
      Log In
    `;
  }
});

accessForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const submitter = event.submitter;
  if (submitter && submitter.value === "cancel") {
    accessDialog.close();
    return;
  }

  const formData = new FormData(accessForm);
  const payload = Object.fromEntries(formData.entries());

  try {
    const response = await fetch("/api/access-requests", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "Unable to send request.");
    accessDialog.close();
    accessForm.reset();
    showMessage("Access request sent.", "success");
  } catch (error) {
    showMessage(error.message);
  }
});

function openDashboard(user, summary) {
  document.querySelector("#roleEyebrow").textContent = user.roleLabel || roleLabels[user.role] || "PaperBriq";
  document.querySelector("#profileInitial").textContent = (user.fullName || user.email || "P").slice(0, 1).toUpperCase();
  renderSummary(summary);
  authScreen.hidden = true;
  dashboardScreen.hidden = false;
}

function renderSummary(summary) {
  const totals = summary.totals || {};
  const paperKg = Number(totals.paperKg || 0);
  const briquettes = Number(totals.briquettes || 0);
  const contributors = Number(totals.contributors || 0);
  const co2KgAvoided = Number(totals.co2KgAvoided || 0);

  document.querySelector("#mainMetric").textContent = `${formatNumber(paperKg)} kg`;
  document.querySelector("#paperMetric").textContent = `${formatNumber(paperKg)} kg`;
  document.querySelector("#briqMetric").textContent = formatNumber(briquettes);
  document.querySelector("#contributorMetric").textContent = formatNumber(contributors);
  document.querySelector("#co2Metric").textContent = `${formatNumber(co2KgAvoided)} kg`;

  const targetPercent = Math.min(99, Math.max(8, Math.round((paperKg / 620) * 100)));
  document.querySelector("#ringValue").textContent = `${targetPercent}%`;

  const collectionList = document.querySelector("#collectionsList");
  collectionList.innerHTML = "";
  (summary.collections || []).forEach((item) => {
    collectionList.insertAdjacentHTML("beforeend", `
      <article class="activity-item">
        <span class="item-icon" aria-hidden="true">kg</span>
        <div>
          <strong>${escapeHtml(item.barangay)}</strong>
          <span>${formatNumber(item.paperWeightKg)} kg paper - ${escapeHtml(item.collectedAt)}</span>
        </div>
        <span class="status-pill">${escapeHtml(item.status)}</span>
      </article>
    `);
  });

  const batchList = document.querySelector("#batchList");
  batchList.innerHTML = "";
  (summary.batches || []).forEach((item) => {
    batchList.insertAdjacentHTML("beforeend", `
      <article class="batch-item">
        <div>
          <strong>${escapeHtml(item.batchCode)}</strong>
          <span>${formatNumber(item.briquettesProduced)} briquettes produced</span>
        </div>
        <span class="status-pill">${escapeHtml(item.qualityStatus)}</span>
      </article>
    `);
  });
}

function showMessage(message, type = "error") {
  formMessage.textContent = message;
  formMessage.style.color = type === "success"
    ? "var(--green-700)"
    : type === "neutral"
      ? "var(--muted)"
      : "var(--danger)";
}

function formatNumber(value) {
  return new Intl.NumberFormat("en-PH", {
    maximumFractionDigits: Number(value) % 1 === 0 ? 0 : 2
  }).format(Number(value) || 0);
}

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#039;"
  }[char]));
}
