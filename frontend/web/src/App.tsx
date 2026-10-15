import { useState } from "react";
import { BrowserRouter, Navigate, Route, Routes, useNavigate } from "react-router-dom";
import "./App.css";
import LoginPage from "./SigninUser/LoginPage";
import OverviewPage from "./admin/OverviewPage";
import ReportsPage from "./admin/ReportsPage";
import IotPage from "./admin/IotPage";
import DashboardLayout from "./routes/DashboardLayout";

function getDefaultRoute(user) {
  return ["admin", "barangay_staff"].includes(user?.role) ? "/monitoring" : "/signin";
}

function AppRoutes() {
  const navigate = useNavigate();
  const [currentUser, setCurrentUser] = useState(() => readStoredValue("paperbriqUser"));
  const [summary, setSummary] = useState(() => readStoredValue("paperbriqSummary"));

  function handleLogin(user, nextSummary, keepLoggedIn = false) {
    setCurrentUser(user);
    setSummary(nextSummary);
    const storage = keepLoggedIn ? localStorage : sessionStorage;
    clearStoredSession();
    storage.setItem("paperbriqUser", JSON.stringify(user));
    storage.setItem("paperbriqSummary", JSON.stringify(nextSummary));
    navigate(getDefaultRoute(user), { replace: true });
  }

  function handleLogout() {
    setCurrentUser(null);
    setSummary(null);
    clearStoredSession();
    navigate("/signin", { replace: true });
  }

  function handleUserUpdate(nextUser) {
    setCurrentUser((current) => {
      const updatedUser = { ...(current || {}), ...nextUser };
      writeStoredUser(updatedUser);
      return updatedUser;
    });
  }

  return (
    <Routes>
      <Route path="/" element={<Navigate to={currentUser ? getDefaultRoute(currentUser) : "/signin"} replace />} />
      <Route
        path="/signin"
        element={currentUser ? <Navigate to={getDefaultRoute(currentUser)} replace /> : <LoginPage onLogin={handleLogin} />}
      />

      <Route
        element={
          currentUser ? (
            <DashboardLayout user={currentUser} summary={summary} onLogout={handleLogout} onUserUpdate={handleUserUpdate} />
          ) : (
            <Navigate to="/signin" replace />
          )
        }
      >
        <Route path="/monitoring" element={<RolePage user={currentUser} roles={["admin", "barangay_staff"]}><OverviewPage /></RolePage>} />
        <Route path="/monitoring/telemetry" element={<RolePage user={currentUser} roles={["admin", "barangay_staff"]}><IotPage /></RolePage>} />
        <Route path="/monitoring/history" element={<RolePage user={currentUser} roles={["admin", "barangay_staff"]}><ReportsPage /></RolePage>} />
      </Route>

      <Route path="*" element={<Navigate to={currentUser ? getDefaultRoute(currentUser) : "/signin"} replace />} />
    </Routes>
  );
}

function RolePage({ user, roles, children }) {
  if (!user) return <Navigate to="/signin" replace />;
  if (!roles.includes(user.role)) return <Navigate to={getDefaultRoute(user)} replace />;
  return children;
}

function readStoredValue(key) {
  try {
    const value = sessionStorage.getItem(key) || localStorage.getItem(key);
    return value ? JSON.parse(value) : null;
  } catch (error) {
    return null;
  }
}

function clearStoredSession() {
  sessionStorage.removeItem("paperbriqUser");
  sessionStorage.removeItem("paperbriqSummary");
  localStorage.removeItem("paperbriqUser");
  localStorage.removeItem("paperbriqSummary");
}

function writeStoredUser(user) {
  const storage = localStorage.getItem("paperbriqUser") ? localStorage : sessionStorage;
  storage.setItem("paperbriqUser", JSON.stringify(user));
}

function App() {
  return (
    <BrowserRouter>
      <AppRoutes />
    </BrowserRouter>
  );
}

export default App;
