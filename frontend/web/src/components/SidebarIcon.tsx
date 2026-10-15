export function SidebarIcon({ name }) {
  const icons = {
    dashboard: (
      <>
        <path d="M4 10.5 12 4l8 6.5" />
        <path d="M6.5 10v9h11v-9" />
        <path d="M10 19v-5h4v5" />
      </>
    ),
    staff: (
      <>
        <path d="M8.5 11a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z" />
        <path d="M3.5 19a5 5 0 0 1 10 0" />
        <path d="M16 10.5a2.5 2.5 0 1 0 0-5" />
        <path d="M15.5 14a4.5 4.5 0 0 1 5 5" />
      </>
    ),
    qr: (
      <>
        <path d="M5 5h5v5H5z" />
        <path d="M14 5h5v5h-5z" />
        <path d="M5 14h5v5H5z" />
        <path d="M14 14h2v2h-2z" />
        <path d="M17 17h2v2h-2z" />
        <path d="M19 14v2" />
        <path d="M14 19h2" />
      </>
    ),
    reports: (
      <>
        <path d="M6 4h9l3 3v13H6z" />
        <path d="M15 4v4h4" />
        <path d="M9 16v-4" />
        <path d="M12 16v-7" />
        <path d="M15 16v-5" />
      </>
    ),
    iot: (
      <>
        <path d="M5 10a10 10 0 0 1 14 0" />
        <path d="M8 13a6 6 0 0 1 8 0" />
        <path d="M11 16a2 2 0 0 1 2 0" />
        <path d="M12 19h.01" />
      </>
    ),
    rewards: (
      <>
        <path d="M5 10h14v10H5z" />
        <path d="M4 7h16v3H4z" />
        <path d="M12 7v13" />
        <path d="M12 7c-3-3-6-2-6 0 0 2 3 2 6 0Z" />
        <path d="M12 7c3-3 6-2 6 0 0 2-3 2-6 0Z" />
      </>
    ),
    settings: (
      <>
        <path d="M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z" />
        <path d="M12 3v2" />
        <path d="M12 19v2" />
        <path d="m4.9 4.9 1.4 1.4" />
        <path d="m17.7 17.7 1.4 1.4" />
        <path d="M3 12h2" />
        <path d="M19 12h2" />
        <path d="m4.9 19.1 1.4-1.4" />
        <path d="m17.7 6.3 1.4-1.4" />
      </>
    )
  };

  return (
    <svg className="nav-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false">
      {icons[name] || icons.dashboard}
    </svg>
  );
}
