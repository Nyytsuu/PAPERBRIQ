import { useEffect, useRef, useState } from "react";
import { useOutletContext } from "react-router-dom";
import { ActionCard, Field, Panel } from "../components/ui";

export default function StaffToolsPage(props = {}) {
  const outlet = useOutletContext() || {};
  const { user, onSectionChange } = { ...outlet, ...props };
  const [qrMessage, setQrMessage] = useState("");
  const [qrMessageType, setQrMessageType] = useState("error");
  const [isSubmitting, setSubmitting] = useState(false);
  const [processNote, setProcessNote] = useState("");
  const [codeValue, setCodeValue] = useState("");
  const [isScannerOpen, setScannerOpen] = useState(false);
  const [scannerMessage, setScannerMessage] = useState("");
  const videoRef = useRef(null);
  const streamRef = useRef(null);
  const scanTimerRef = useRef(null);

  useEffect(() => {
    if (!isScannerOpen) return undefined;

    let cancelled = false;

    async function openCameraScanner() {
      const BarcodeDetectorApi = window.BarcodeDetector;
      if (!BarcodeDetectorApi || !navigator.mediaDevices?.getUserMedia) {
        setScannerMessage("Camera QR scanning is not available in this browser. Enter the contributor code manually.");
        return;
      }

      try {
        setScannerMessage("Opening camera...");
        const stream = await navigator.mediaDevices.getUserMedia({
          video: { facingMode: { ideal: "environment" } },
          audio: false
        });
        if (cancelled) {
          stream.getTracks().forEach((track) => track.stop());
          return;
        }

        streamRef.current = stream;
        const video = videoRef.current;
        video.srcObject = stream;
        await video.play();

        const detector = new BarcodeDetectorApi({ formats: ["qr_code"] });
        setScannerMessage("Point the camera at the contributor QR code.");

        const scanFrame = async () => {
          if (cancelled || !videoRef.current) return;
          try {
            const codes = await detector.detect(videoRef.current);
            const rawValue = codes?.[0]?.rawValue?.trim();
            if (rawValue) {
              setCodeValue(rawValue);
              setScannerOpen(false);
              setQrMessageType("success");
              setQrMessage("QR scanned. Enter the contributor name and paper weight, then submit.");
              return;
            }
          } catch (error) {
            setScannerMessage("Scanner could not read the frame. Try brighter lighting or use manual entry.");
          }
          scanTimerRef.current = window.setTimeout(scanFrame, 450);
        };

        scanFrame();
      } catch (error) {
        setScannerMessage("Camera permission was blocked or no camera was found. Enter the contributor code manually.");
      }
    }

    openCameraScanner();

    return () => {
      cancelled = true;
      stopCameraScanner();
    };
  }, [isScannerOpen]);

  function stopCameraScanner() {
    if (scanTimerRef.current) {
      window.clearTimeout(scanTimerRef.current);
      scanTimerRef.current = null;
    }
    if (streamRef.current) {
      streamRef.current.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
    }
  }

  function closeScanner() {
    stopCameraScanner();
    setScannerOpen(false);
  }

  async function submitScan(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const formData = new FormData(form);
    const code = String(formData.get("code") || "").trim();
    const contributor = String(formData.get("contributor") || "").trim();
    const weightKg = Number(formData.get("weightKg") || 0);
    if (!code || !contributor || !Number.isFinite(weightKg) || weightKg <= 0) {
      setQrMessageType("error");
      setQrMessage("Contributor code, contributor name, and paper weight are required.");
      return;
    }

    const payload = {
      code,
      contributor,
      barangay: user.barangay || "Unassigned",
      weightKg,
      pointsEarned: Math.round(weightKg * 10)
    };
    setSubmitting(true);
    setQrMessage("");

    try {
      const response = await fetch("/api/qr-scans", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to record QR scan.");
      setQrMessageType("success");
      setQrMessage(`Recorded scan. Points earned: ${result.scan.pointsEarned}.`);
      setCodeValue("");
      form.reset();
    } catch (error) {
      setQrMessageType("error");
      setQrMessage(error.message);
    } finally {
      setSubmitting(false);
    }
  }

  async function sendProcessAction(action) {
    setProcessNote("");
    await fetch("/api/process/actions", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action, source: "web", role: user.role })
    });
    setProcessNote(action === "start" ? "Start command sent." : "Stop command sent.");
  }

  return (
    <section className="operations-grid">
      <Panel title="Record QR Scan" subtitle="Log a contributor donation from the barangay station.">
        <form className="staff-form" onSubmit={submitScan}>
          <button className="secondary-button scan-camera-button" type="button" onClick={() => setScannerOpen(true)}>
            Use Camera Scanner
          </button>
          <Field
            label="Contributor Code"
            name="code"
            placeholder="Scan or enter code"
            value={codeValue}
            onChange={(event) => setCodeValue(event.target.value)}
          />
          <Field label="Contributor Name" name="contributor" placeholder="Contributor name" />
          <Field label="Paper Weight (kg)" name="weightKg" type="number" min="0" step="0.1" placeholder="0.0" />
          <button className="primary-button" type="submit" disabled={isSubmitting}>
            {isSubmitting ? "Submitting" : "Submit Scan"}
          </button>
          <p className={`form-message left ${qrMessageType}`}>{qrMessage}</p>
        </form>
      </Panel>

      <Panel title="Machine Controls" subtitle="Operate the briquette process beside the machine.">
        <div className="action-grid">
          <ActionCard title="Start Process" subtitle="Begin collection cycle" onClick={() => sendProcessAction("start")} />
          <ActionCard title="Stop Process" subtitle="Pause active cycle" onClick={() => sendProcessAction("stop")} />
          <ActionCard title="Machine Status" subtitle="Check IoT readings" onClick={() => onSectionChange("iot")} />
          <ActionCard title="Confirm Reward" subtitle="Review claims" onClick={() => onSectionChange("rewards")} />
        </div>
        <p className="muted-copy action-note">{processNote}</p>
      </Panel>

      {isScannerOpen && (
        <div className="modal-backdrop" role="presentation" onClick={closeScanner}>
          <section
            className="scanner-modal"
            role="dialog"
            aria-modal="true"
            aria-labelledby="scannerModalTitle"
            onClick={(event) => event.stopPropagation()}
          >
            <div className="section-title">
              <div>
                <h2 id="scannerModalTitle">Scan Contributor QR</h2>
                <p>{scannerMessage || "Preparing scanner..."}</p>
              </div>
              <button className="secondary-button" type="button" onClick={closeScanner}>Close</button>
            </div>
            <div className="scanner-frame">
              <video ref={videoRef} muted playsInline />
              <span aria-hidden="true" />
            </div>
          </section>
        </div>
      )}
    </section>
  );
}
