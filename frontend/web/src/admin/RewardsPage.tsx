import React, { useState } from "react";
import { useOutletContext } from "react-router-dom";
import { DataList, DataRow, Panel } from "../components/ui";
import { formatNumber } from "../api/format";

export default function RewardsPage(props = {}) {
  const outlet = useOutletContext() || {};
  const { user } = { ...outlet, ...props };
  const [rewardsData, setRewardsData] = useState(null);
  const [message, setMessage] = useState("Loading rewards...");
  const [messageType, setMessageType] = useState("success");
  const [lastSyncedAt, setLastSyncedAt] = useState(null);
  const [isRefreshing, setRefreshing] = useState(false);
  const [imagePreview, setImagePreview] = useState("");
  const [isSaving, setSaving] = useState(false);
  const [selectedReward, setSelectedReward] = useState(null);
  const [isRedeeming, setRedeeming] = useState(false);
  const [redeemMessage, setRedeemMessage] = useState("");
  const isStaff = user?.role === "barangay_staff";
  const isAdmin = user?.role === "admin";

  React.useEffect(() => {
    loadRewards();
    const intervalId = window.setInterval(() => loadRewards({ silent: true }), 7000);

    return () => window.clearInterval(intervalId);
  }, []);

  async function loadRewards(options = {}) {
    const { silent = false } = options;
    setRefreshing(true);
    if (!silent) {
      setMessageType("success");
      setMessage(rewardsData ? "Refreshing rewards..." : "Loading rewards...");
    }

    try {
      const response = await fetch("/api/rewards");
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to load rewards.");
      setRewardsData(result);
      setLastSyncedAt(new Date());
      if (!silent) setMessage("");
    } catch (error) {
      setMessageType("error");
      setMessage(error.message);
    } finally {
      setRefreshing(false);
    }
  }

  function handleImageChange(event) {
    const file = event.target.files?.[0];
    if (!file) {
      setImagePreview("");
      return;
    }

    const reader = new FileReader();
    reader.onload = () => setImagePreview(String(reader.result || ""));
    reader.readAsDataURL(file);
  }

  async function handleCreateReward(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const formData = new FormData(form);
    const payload = {
      name: formData.get("name"),
      category: formData.get("category"),
      pointsCost: Number(formData.get("pointsCost") || 0),
      stockQuantity: Number(formData.get("stockQuantity") || 0),
      description: formData.get("description"),
      imageUrl: imagePreview || formData.get("imageUrl"),
      creatorRole: user.role,
      creatorName: user.fullName || user.email
    };

    setSaving(true);
    setMessage("");

    try {
      const response = await fetch("/api/rewards", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to add reward item.");
      form.reset();
      setImagePreview("");
      setMessageType("success");
      setMessage(`${result.reward.name} added to the reward catalog.`);
      await loadRewards();
    } catch (error) {
      setMessageType("error");
      setMessage(error.message);
    } finally {
      setSaving(false);
    }
  }

  function openRewardDetails(reward) {
    setSelectedReward(reward);
    setRedeemMessage("");
  }

  async function handleRedeemReward() {
    if (!selectedReward) return;

    setRedeeming(true);
    setRedeemMessage("");

    try {
      const response = await fetch("/api/rewards/redeem", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          rewardId: selectedReward.id,
          email: user?.email || "",
          role: user?.role || ""
        })
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to redeem reward.");
      setSelectedReward(null);
      setMessageType("success");
      setMessage(`${result.redemption.rewardName || selectedReward.name} redeemed.`);
      await loadRewards({ silent: true });
    } catch (error) {
      setRedeemMessage(error.message);
    } finally {
      setRedeeming(false);
    }
  }

  const rewards = rewardsData?.rewards || [];
  const monitor = rewardsData?.monitor || {};
  const history = rewardsData?.history || [];
  const syncLabel = lastSyncedAt ? `Live - synced ${lastSyncedAt.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}` : "Connecting";

  return (
    <section className="rewards-page">
      <Panel
        title={isStaff ? "Barangay Rewards" : "Rewards Monitoring"}
        subtitle={isStaff ? "Add local reward items and review contributor claim options." : "Monitor reward inventory, points, and redemption movement."}
        action={<button className="text-button" type="button" onClick={() => loadRewards()} disabled={isRefreshing}>{isRefreshing ? "Refreshing" : "Refresh"}</button>}
      >
        {message && <p className={`form-message left ${messageType}`}>{message}</p>}

        <div className="reward-monitor-grid">
          <RewardStat label="Reward Items" value={formatNumber(monitor.rewardItems || rewards.length)} />
          <RewardStat label="Total Stock" value={formatNumber(monitor.totalStock || 0)} />
          <RewardStat label="Points Redeemed" value={`${formatNumber(monitor.pointsRedeemed || rewardsData?.totals?.pointsRedeemedThisMonth || 0)} pts`} />
          <RewardStat label={isAdmin ? "Items To Watch" : "Low Stock"} value={formatNumber(monitor.lowStockItems || 0)} />
        </div>
      </Panel>

      <div className={`reward-management-grid ${isAdmin ? "admin-rewards-grid" : ""}`}>
        {isStaff && (
          <Panel title="Add Reward Item" subtitle="Barangay staff can add reward options for contributor claims.">
            <form className="reward-form" onSubmit={handleCreateReward}>
              <label className="reward-image-picker">
                <span>Reward Picture</span>
                <input type="file" accept="image/*" onChange={handleImageChange} />
                <strong>{imagePreview ? "Image selected" : "Upload image"}</strong>
                {imagePreview ? <img src={imagePreview} alt="" /> : <i aria-hidden="true">+</i>}
              </label>

              <div className="reward-form-grid">
                <label className="field-group">
                  <span>Reward Name</span>
                  <span className="input-shell">
                    <input name="name" placeholder="Eco rice pack, tote bag, voucher..." required />
                  </span>
                </label>
                <label className="field-group">
                  <span>Category</span>
                  <span className="input-shell">
                    <select name="category" defaultValue="Eco Products">
                      <option>Eco Products</option>
                      <option>Vouchers</option>
                      <option>Barangay Goods</option>
                      <option>Green Donations</option>
                      <option>Certificates</option>
                    </select>
                  </span>
                </label>
                <label className="field-group">
                  <span>Reward Points</span>
                  <span className="input-shell">
                    <input name="pointsCost" type="number" min="1" defaultValue="100" required />
                  </span>
                </label>
                <label className="field-group">
                  <span>Stock Quantity</span>
                  <span className="input-shell">
                    <input name="stockQuantity" type="number" min="0" defaultValue="10" required />
                  </span>
                </label>
              </div>

              <label className="field-group">
                <span>Image URL backup</span>
                <span className="input-shell">
                  <input name="imageUrl" placeholder="Optional image link if no upload is used" />
                </span>
              </label>

              <label className="field-group">
                <span>Description</span>
                <span className="input-shell textarea-shell">
                  <textarea name="description" rows="4" placeholder="What the reward is, claim notes, or release instructions." />
                </span>
              </label>

              <button className="primary-button" type="submit" disabled={isSaving}>
                {isSaving ? "Adding reward" : "Add Reward Item"}
              </button>
            </form>
          </Panel>
        )}

        <Panel title={isAdmin ? "Reward Catalog Monitor" : "Available Reward Catalog"} subtitle={isAdmin ? "All reward items created for contributor claims." : "Items visible to admins and contributors."}>
          <p className="reward-sync-note">{syncLabel}</p>
          {rewards.length ? (
            <div className="reward-catalog-grid">
              {rewards.map((reward) => (
                <button className="reward-card reward-tile" type="button" key={reward.id} onClick={() => openRewardDetails(reward)}>
                  <RewardImage reward={reward} />
                  <strong>{formatNumber(reward.pointsCost)} pts</strong>
                </button>
              ))}
            </div>
          ) : (
            <p className="muted-copy">No reward items from the backend yet.</p>
          )}
        </Panel>
      </div>

      <Panel title={isAdmin ? "Points and Redemption Activity" : "Recent Reward Activity"} subtitle="Latest point transactions connected to rewards.">
        <DataList items={history} empty="No reward activity yet.">
          {(item) => (
            <DataRow
              title={item.title}
              subtitle={`${item.happenedAt} - ${item.type}`}
              pill={`${formatNumber(item.points)} pts`}
            />
          )}
        </DataList>
      </Panel>

      {selectedReward && (
        <div className="modal-backdrop" role="presentation" onClick={() => setSelectedReward(null)}>
          <section
            className="reward-detail-modal"
            role="dialog"
            aria-modal="true"
            aria-labelledby="rewardDetailTitle"
            onClick={(event) => event.stopPropagation()}
          >
            <div className="reward-detail-image">
              <RewardImage reward={selectedReward} />
            </div>
            <h2 id="rewardDetailTitle">{selectedReward.name}</h2>
            <p>{selectedReward.description || "No reward details added yet."}</p>
            <div className="reward-detail-grid">
              <span><b>{formatNumber(selectedReward.pointsCost)}</b> pts</span>
              <span><b>{formatNumber(selectedReward.stockQuantity || 0)}</b> stock</span>
              <span><b>{selectedReward.category || "Reward"}</b> category</span>
              {selectedReward.createdByName && <span><b>{selectedReward.createdByName}</b> added</span>}
            </div>
            {redeemMessage && <p className="form-message left error">{redeemMessage}</p>}
            <div className="confirm-actions">
              <button className="secondary-button" type="button" onClick={() => setSelectedReward(null)}>
                Cancel
              </button>
              <button className="primary-button confirm-button" type="button" onClick={handleRedeemReward} disabled={isRedeeming || Number(selectedReward.stockQuantity || 0) <= 0}>
                {Number(selectedReward.stockQuantity || 0) <= 0 ? "Out of stock" : isRedeeming ? "Redeeming" : "Redeem"}
              </button>
            </div>
          </section>
        </div>
      )}
    </section>
  );
}

function RewardImage({ reward }) {
  if (reward?.imageUrl) {
    return (
      <div className="reward-card-image">
        <img src={reward.imageUrl} alt="" />
      </div>
    );
  }

  return (
    <div className="reward-card-image">
      <span>{String(reward?.name || "R").slice(0, 1).toUpperCase()}</span>
    </div>
  );
}

function RewardStat({ label, value }) {
  return (
    <article>
      <span>{label}</span>
      <strong>{value}</strong>
    </article>
  );
}
