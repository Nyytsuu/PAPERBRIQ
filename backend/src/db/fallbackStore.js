const { randomUUID } = require("crypto");
const { ROLE_LABELS } = require("../config/roles");
const { fallbackSummary, fallbackUsers } = require("../data/fallbackData");
const { pageData } = require("../data/pageData");

function createFallbackStore() {
  return {
    mode: "demo memory",
    configured: false,
    async close() {},
    async login(email, password, role) {
      const user = fallbackUsers.find((candidate) => (
        candidate.email === email &&
        candidate.password === password &&
        candidate.role === role
      ));

      if (!user) return null;

      return {
        id: user.id,
        fullName: user.fullName,
        email: user.email,
        role: user.role,
        roleLabel: ROLE_LABELS[user.role],
        barangay: user.barangay
      };
    },
    async getSummary() {
      return fallbackSummary;
    },
    async getDashboard() {
      return { ...pageData.dashboard, summary: fallbackSummary };
    },
    async getProcess() {
      return pageData.process;
    },
    async createProcessAction(action) {
      return {
        id: `demo-${randomUUID()}`,
        action: action.action || "start",
        status: "accepted",
        created_at: new Date().toISOString()
      };
    },
    async getIotMonitoring() {
      return pageData.iot;
    },
    async getContributions() {
      return pageData.contributions;
    },
    async getRewards() {
      return pageData.rewards;
    },
    async redeemReward(request) {
      return {
        id: `demo-${randomUUID()}`,
        rewardId: request.rewardId,
        status: "redeemed",
        created_at: new Date().toISOString()
      };
    },
    async getReports() {
      return pageData.reports;
    },
    async getHistory() {
      return pageData.history;
    },
    async getSettings() {
      return pageData.settings;
    },
    async updateSettings(settings) {
      return {
        settings: { ...pageData.settings, ...settings },
        updated_at: new Date().toISOString()
      };
    },
    async getQrScans() {
      return pageData.qrScans;
    },
    async createQrScan(scan) {
      return {
        id: `demo-${randomUUID()}`,
        code: scan.code || "manual",
        status: "success",
        pointsEarned: 15,
        created_at: new Date().toISOString()
      };
    },
    async createAccessRequest() {
      return {
        id: `demo-${randomUUID()}`,
        status: "pending",
        created_at: new Date().toISOString()
      };
    }
  };
}

module.exports = { createFallbackStore };
