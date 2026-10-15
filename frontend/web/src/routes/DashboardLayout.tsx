import React, { useState } from "react";
import { Outlet, useLocation, useNavigate } from "react-router-dom";
import { roleLabels, sectionCopy } from "./navigation";
import { ConfirmModal } from "../components/ui";
import { SidebarIcon } from "../components/SidebarIcon";
import { formatNumber } from "../api/format";
import { connectLiveStream } from "../api/liveStream";

const routeBySection = {
  overview: "/monitoring",
  iot: "/monitoring/telemetry",
  reports: "/monitoring/history"
};

function getActiveSection(pathname) {
  const entries = Object.entries(routeBySection);
  return entries.find(([, path]) => path === pathname)?.[0] || "overview";
}

export default function DashboardLayout({ user, summary, onLogout, onUserUpdate }) {
  const location = useLocation();
  const navigate = useNavigate();
  const activeSection = getActiveSection(location.pathname);
  const copy = sectionCopy[activeSection] || sectionCopy.overview;
  const [isSidebarCompact, setSidebarCompact] = useState(false);
  const [isLogoutOpen, setLogoutOpen] = useState(false);
  const [liveDashboard, setLiveDashboard] = useState(null);
  const [dashboardMessage, setDashboardMessage] = useState("");
  const dashboardSummary = liveDashboard?.summary || summary;
  const systemStatus = liveDashboard?.systemStatus || {};
  const rainwaterLevel = Number(systemStatus.rainwaterLevel ?? 0);
  const powerOutputWatts = Number(systemStatus.powerOutputWatts ?? 0);
  const sidebarWaterPercent = Math.min(100, Math.max(0, rainwaterLevel));
  const statusLabel = dashboardMessage ? "Backend reconnecting" : `${systemStatus.status || "No machine data"} - Backend connected`;
  const visibleNav = [
    ["overview", "Live Overview", "dashboard", "System health", routeBySection.overview],
    ["iot", "Telemetry", "iot", "Sensors & alerts", routeBySection.iot],
    ["reports", "History", "reports", "Trends & impact", routeBySection.reports]
  ];

  function navigateToSection(sectionKey) {
    navigate(routeBySection[sectionKey] || routeBySection.overview);
  }

  function handleCompactUserKeyDown(event) {
    if (!isSidebarCompact) return;
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      setLogoutOpen(true);
    }
  }

  React.useEffect(() => {
    let isMounted = true;
    let intervalId;
    let closeStream = () => {};

    async function loadDashboard() {
      try {
        const response = await fetch("/api/dashboard");
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || "Unable to load live dashboard.");
        if (!isMounted) return;
        setLiveDashboard(result);
        setDashboardMessage("");
      } catch (error) {
        if (!isMounted) return;
        setDashboardMessage(error.message);
      }
    }

    loadDashboard();

    if (typeof EventSource === "undefined") {
      intervalId = window.setInterval(loadDashboard, 5000);
    } else {
      closeStream = connectLiveStream(["dashboard"], {
        onOpen: () => {
          if (isMounted) setDashboardMessage("");
        },
        onSnapshot: (payload) => {
          if (!isMounted || !payload.dashboard) return;
          setLiveDashboard(payload.dashboard);
          setDashboardMessage("");
        },
        onError: (message) => {
          if (isMounted) setDashboardMessage(message);
        }
      });
    }

    return () => {
      isMounted = false;
      if (intervalId) window.clearInterval(intervalId);
      closeStream();
    };
  }, []);

  return (
    <main className={`dashboard-screen ${isSidebarCompact ? "sidebar-compact" : ""}`}>
      <aside className="sidebar" aria-label="Main navigation">
        <button
          className="sidebar-brand"
          type="button"
          aria-label={isSidebarCompact ? "Expand sidebar" : "Collapse sidebar"}
          aria-expanded={!isSidebarCompact}
          onClick={() => setSidebarCompact(!isSidebarCompact)}
        >
          <img src="/assets/images/paperbriq-logo-mark.svg" alt="" aria-hidden="true" />
          <div>
            <strong>PAPERBRIQ</strong>
            <span>Monitoring System</span>
          </div>
        </button>

        <nav className="side-nav">
          {visibleNav.map(([key, label, icon, description, path]) => (
            <button
              className={activeSection === key ? "active" : ""}
              key={key}
              type="button"
              aria-current={activeSection === key ? "page" : undefined}
              aria-label={label}
              title={`${label} - ${description}`}
              data-section={key}
              onClick={() => navigate(path)}
            >
              <span className="nav-mark"><SidebarIcon name={icon} /></span>
              <span className="nav-copy">
                <strong>{label}</strong>
                <small>{description}</small>
              </span>
              <span className="nav-arrow" aria-hidden="true">›</span>
            </button>
          ))}
        </nav>

        <div className="sidebar-status">
          <span className="live-dot" aria-hidden="true" />
          <div>
            <strong>Machine online</strong>
            <small>{formatNumber(powerOutputWatts)} W output</small>
          </div>
          <div className="sidebar-meter" aria-hidden="true"><span style={{ width: `${sidebarWaterPercent}%` }} /></div>
        </div>

        <div
          className="sidebar-user"
          role={isSidebarCompact ? "button" : undefined}
          tabIndex={isSidebarCompact ? 0 : undefined}
          aria-label={isSidebarCompact ? "Open logout confirmation" : undefined}
          onClick={() => {
            if (isSidebarCompact) setLogoutOpen(true);
          }}
          onKeyDown={handleCompactUserKeyDown}
        >
          <span className="sidebar-avatar">
            {user.profileImageUrl ? (
              <img src={user.profileImageUrl} alt="" aria-hidden="true" />
            ) : (
              (user.fullName || user.email || "P").slice(0, 1).toUpperCase()
            )}
          </span>
          <div>
            <strong>{user.fullName || user.email}</strong>
            <small>{roleLabels[user.role]}</small>
          </div>
          <button className="sidebar-logout" type="button" onClick={() => setLogoutOpen(true)}>
            Log Out
          </button>
        </div>
      </aside>

      <section className="workspace">
        <header className="topbar">
          <div className="topbar-copy">
            <p className="eyebrow">PaperBriq Monitoring</p>
            <h1>{copy.title}</h1>
            <p>{copy.subtitle}</p>
            <div className="topbar-mini" aria-label="Workspace summary">
              <span><strong>{liveDashboard?.displayTime || "Live"}</strong><small>last sync</small></span>
              <span><strong>MySQL</strong><small>data mode</small></span>
              <span><strong>Read only</strong><small>monitoring mode</small></span>
            </div>
          </div>
          <aside className="topbar-panel" aria-label="Live workspace status">
            <div className="panel-status-line">
              <span className="live-dot" aria-hidden="true" />
              <strong>{statusLabel}</strong>
            </div>
            <div className="panel-readings">
              <span><strong>{formatNumber(rainwaterLevel)}%</strong><small>Rainwater</small></span>
              <span><strong>{formatNumber(powerOutputWatts)} W</strong><small>Power</small></span>
            </div>
          </aside>
        </header>

        <Outlet
          context={{
            user,
            summary: dashboardSummary,
            dashboard: liveDashboard,
            dashboardMessage,
            onSectionChange: navigateToSection,
            onUserUpdate
          }}
        />
      </section>

      {isLogoutOpen && (
        <ConfirmModal
          title="Log out?"
          message="You will return to the PaperBriq sign-in page. Any unsaved form changes should be saved first."
          confirmLabel="Log out"
          tone="danger"
          onCancel={() => setLogoutOpen(false)}
          onConfirm={() => {
            setLogoutOpen(false);
            onLogout();
          }}
        />
      )}
    </main>
  );
}
