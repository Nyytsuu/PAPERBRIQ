const ROLE_LABELS = {
  admin: "Admin",
  barangay_staff: "Barangay Staff",
  contributor: "User"
};

function normalizeRole(value) {
  const role = String(value || "").trim();
  return Object.prototype.hasOwnProperty.call(ROLE_LABELS, role) ? role : null;
}

module.exports = {
  ROLE_LABELS,
  normalizeRole
};
