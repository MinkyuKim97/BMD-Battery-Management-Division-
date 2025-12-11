// src/App.jsx

// import "../style.css";  // 예전 스타일은 사용 안 함
import "./App.css";

import { useEffect, useMemo, useState } from "react";
import { db } from "../firebaseConfig.js";
import {
  collection,
  onSnapshot,
  query,
  orderBy,
  addDoc,
  doc,
  updateDoc,
  deleteDoc,
} from "firebase/firestore";

// ==== helpers ======================================================

const DISPLAY_YEAR_OFFSET = 100;

function buildFullName(first, last) {
  const f = (first || "").trim();
  const l = (last || "").trim();
  if (!f && !l) return "";
  if (!l) return f;
  if (!f) return l;
  return `${f} ${l}`;
}

function normalizeName(name) {
  return (name || "").trim().replace(/\s+/g, " ").toLowerCase();
}

function epochToDate(v) {
  if (v === null || v === undefined) return null;

  if (v && typeof v.toDate === "function") return v.toDate();

  if (typeof v === "number") {
    if (v < 1e11) return new Date(v * 1000);
    return new Date(v);
  }

  const n = Number(v);
  if (!Number.isFinite(n)) return null;
  if (n < 1e11) return new Date(n * 1000);
  return new Date(n);
}

function applyYearOffset(date, offsetYears = DISPLAY_YEAR_OFFSET) {
  if (!date) return null;
  const d = new Date(date.getTime());
  d.setFullYear(d.getFullYear() + offsetYears);
  return d;
}

function parseMDYToEpoch(mdy) {
  const s = (mdy || "").trim();
  if (!s) return null;
  const parts = s.split(/[\/\-.]/);
  if (parts.length < 3) return null;

  const m = parseInt(parts[0], 10);
  const d = parseInt(parts[1], 10);
  const y = parseInt(parts[2], 10);

  if (!Number.isFinite(m) || !Number.isFinite(d) || !Number.isFinite(y))
    return null;
  if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1900) return null;

  const date = new Date(y, m - 1, d);
  if (Number.isNaN(date.getTime())) return null;

  return Math.floor(date.getTime() / 1000);
}

function formatMDY(value) {
  const base = epochToDate(value);
  if (!base) return "-";
  const d = applyYearOffset(base);
  const mm = String(d.getMonth() + 1).padStart(2, "0");
  const dd = String(d.getDate()).padStart(2, "0");
  const yyyy = d.getFullYear();
  return `${mm}/${dd}/${yyyy}`;
}

function formatDateTime(value) {
  const base = epochToDate(value);
  if (!base) return "—";
  const d = applyYearOffset(base);
  return d.toLocaleString();
}

function computeLifetimeProgress(member) {
  const startDate = epochToDate(member?.lastBatteryReplacementDate);
  const endDate = epochToDate(member?.batteryDueDate);
  if (!startDate || !endDate) return null;

  const start = startDate.getTime();
  const end = endDate.getTime();
  if (!(end > start)) return null;

  const now = Date.now();
  const clamped = Math.min(Math.max(now, start), end);

  const usedRatio = (clamped - start) / (end - start);
  let remainingPercent = Math.round((1 - usedRatio) * 100);
  if (remainingPercent < 0) remainingPercent = 0;
  if (remainingPercent > 100) remainingPercent = 100;

  return { percent: remainingPercent, startDate, endDate };
}

// tendency를 0~10으로 받아서 lastReplacement에서 1~2개월 사이로 due date 계산
function computeDueDateFromLastReplacement(lastEpoch, tendencyRaw) {
  if (lastEpoch == null) return null;
  const lastDate = new Date(lastEpoch * 1000);
  if (Number.isNaN(lastDate.getTime())) return null;

  let t = Number(tendencyRaw);
  if (!Number.isFinite(t)) t = 0;
  if (t < 0) t = 0;
  if (t > 10) t = 10;
  const norm = t / 10; // 0~1

  // base1: +1개월, base2: +2개월
  const base1 = new Date(lastDate.getTime());
  base1.setMonth(base1.getMonth() + 1);
  base1.setHours(0, 0, 0, 0);

  const base2 = new Date(lastDate.getTime());
  base2.setMonth(base2.getMonth() + 2);
  base2.setHours(0, 0, 0, 0);

  const ms1 = base1.getTime();
  const ms2 = base2.getTime();

  // tendency가 높을수록(1에 가까울수록) base1 쪽으로 끌어당김
  const targetMs = ms2 - norm * (ms2 - ms1);

  return Math.floor(targetMs / 1000);
}

// ==== main app =====================================================

export function App() {
  const [firstNameInput, setFirstNameInput] = useState("");
  const [lastNameInput, setLastNameInput] = useState("");

  const [currentName, setCurrentName] = useState("");
  const [newMemberName, setNewMemberName] = useState("");
  const [connectError, setConnectError] = useState("");

  const [members, setMembers] = useState([]);
  const [membersLoaded, setMembersLoaded] = useState(false);

  const [createCountry, setCreateCountry] = useState("");
  const [createBirth, setCreateBirth] = useState("");
  const [createLastReplacement, setCreateLastReplacement] = useState("");
  const [createVisa, setCreateVisa] = useState("");
  const [createTendency, setCreateTendency] = useState("");
  const [creating, setCreating] = useState(false);
  const [createError, setCreateError] = useState("");

  useEffect(() => {
    document.title = "BMD";
  }, []);

  const todayStr = useMemo(() => {
    const base = new Date();
    const d = applyYearOffset(base);
    const mm = String(d.getMonth() + 1).padStart(2, "0");
    const dd = String(d.getDate()).padStart(2, "0");
    const yyyy = d.getFullYear();
    return `${mm}/${dd}/${yyyy}`;
  }, []);

  // Firestore: members 실시간 구독
  useEffect(() => {
    const q = query(collection(db, "members"), orderBy("batteryDueDate", "asc"));
    const unsub = onSnapshot(
      q,
      (snap) => {
        const list = snap.docs.map((d) => ({ id: d.id, ...d.data() }));
        setMembers(list);
        setMembersLoaded(true);
      },
      (err) => {
        console.error("members snapshot error:", err);
        setMembersLoaded(true);
      }
    );
    return () => unsub();
  }, []);

  const currentMember = useMemo(() => {
    if (!members.length || !currentName) return null;
    const target = normalizeName(currentName);
    return members.find((m) => normalizeName(m.name) === target) || null;
  }, [members, currentName]);

  const otherMembers = useMemo(() => {
    if (!members.length) return [];
    const me = normalizeName(currentName);
    return members.filter(
      (m) => normalizeName(m.name) && normalizeName(m.name) !== me
    );
  }, [members, currentName]);

  const mainProgress = useMemo(
    () => computeLifetimeProgress(currentMember),
    [currentMember]
  );

  // 배터리 0%일 때 자동으로 Financial Access / Visa Type 변경
  useEffect(() => {
    if (!currentMember || !currentMember.id) return;
    if (!mainProgress) return;

    const desiredFinancial = mainProgress.percent > 0;
    const currentFinancial = !!currentMember.canFinancialTransactions;
    const originalVisa = currentMember.visaTypeOriginal || currentMember.visaType || "";
    const desiredVisa = desiredFinancial ? originalVisa : "Unable";
    const currentVisa = currentMember.visaType || "";

    if (currentFinancial === desiredFinancial && currentVisa === desiredVisa) {
      return;
    }

    const nowEpoch = Math.floor(Date.now() / 1000);

    updateDoc(doc(db, "members", currentMember.id), {
      canFinancialTransactions: desiredFinancial,
      visaType: desiredVisa,
      visaTypeOriginal: originalVisa,
      lastUpdatedClient: nowEpoch,
    }).catch((e) => console.error("auto financial/visa update failed:", e));
  }, [
    currentMember?.id,
    currentMember?.canFinancialTransactions,
    currentMember?.visaType,
    currentMember?.visaTypeOriginal,
    mainProgress?.percent,
  ]);

  // lastReplacement가 바뀔 때마다(다른 장치에서 수정해도) due date 재계산
  useEffect(() => {
    if (!currentMember || !currentMember.id) return;

    const lastEpoch = currentMember.lastBatteryReplacementDate;
    const tendencyVal = currentMember.tendency;

    const recomputed = computeDueDateFromLastReplacement(lastEpoch, tendencyVal);
    if (recomputed == null) return;

    const currentDue = currentMember.batteryDueDate;
    if (Number(currentDue) === recomputed) return;

    const nowEpoch = Math.floor(Date.now() / 1000);

    updateDoc(doc(db, "members", currentMember.id), {
      batteryDueDate: recomputed,
      lastUpdatedClient: nowEpoch,
    }).catch((e) => console.error("auto due-date update failed:", e));
  }, [
    currentMember?.id,
    currentMember?.lastBatteryReplacementDate,
    currentMember?.tendency,
    currentMember?.batteryDueDate,
  ]);

  function handleConnect(e) {
    e?.preventDefault?.();
    if (!membersLoaded) {
      setConnectError("Registry is still loading. Retry in a moment.");
      return;
    }

    const full = buildFullName(firstNameInput, lastNameInput);
    if (!full) {
      setConnectError("First Name / Last Name 을 모두 입력하세요.");
      setNewMemberName("");
      setCurrentName("");
      return;
    }

    const target = normalizeName(full);
    const match = members.find((m) => normalizeName(m.name) === target) || null;

    if (match) {
      setCurrentName(match.name || full);
      setNewMemberName("");
      setConnectError("");
      setCreateError("");
    } else {
      setCurrentName("");
      setNewMemberName(full);
      setConnectError(
        `"${full}" 에 해당하는 기록이 없습니다. 아래에서 신규 등록을 진행해주세요.`
      );
      setCreateError("");
      setCreateCountry("");
      setCreateBirth("");
      setCreateLastReplacement("");
      setCreateVisa("");
      setCreateTendency("");
    }
  }

  async function handleDeleteRecord() {
    if (!membersLoaded) return;

    const full = buildFullName(firstNameInput, lastNameInput);
    if (!full) {
      setConnectError("삭제하려면 First / Last Name 을 모두 입력하세요.");
      return;
    }

    const target = normalizeName(full);
    const match = members.find((m) => normalizeName(m.name) === target) || null;
    if (!match) {
      setConnectError(`"${full}" 에 해당하는 기록이 없어 삭제할 수 없습니다.`);
      return;
    }

    const ok = window.confirm(
      `Really delete record for "${match.name}"?\nThis operation cannot be undone.`
    );
    if (!ok) return;

    try {
      await deleteDoc(doc(db, "members", match.id));
      if (normalizeName(currentName) === normalizeName(match.name)) {
        setCurrentName("");
      }
      setNewMemberName("");
      setConnectError(`"${match.name}" 기록이 삭제되었습니다.`);
      setCreateError("");
    } catch (e) {
      console.error("delete member failed:", e);
      setConnectError(e?.message || "Failed to delete member.");
    }
  }

  async function handleCreateMember(e) {
    e?.preventDefault?.();
    if (!newMemberName) return;

    setCreating(true);
    setCreateError("");

    try {
      const birthEpoch = parseMDYToEpoch(createBirth);
      if (birthEpoch == null) {
        setCreateError("Birth Date 는 MM/DD/YYYY 형식으로 입력해야 합니다.");
        setCreating(false);
        return;
      }

      const lastEpoch = parseMDYToEpoch(createLastReplacement);
      if (lastEpoch == null) {
        setCreateError("Last Replacement 도 MM/DD/YYYY 형식으로 입력해야 합니다.");
        setCreating(false);
        return;
      }

      let tendencyNum = parseInt(createTendency, 10);
      if (!Number.isFinite(tendencyNum)) tendencyNum = 0;
      if (tendencyNum < 0) tendencyNum = 0;
      if (tendencyNum > 10) tendencyNum = 10;

      const dueEpoch = computeDueDateFromLastReplacement(lastEpoch, tendencyNum);
      if (dueEpoch == null) {
        setCreateError("Battery due date 계산에 실패했습니다.");
        setCreating(false);
        return;
      }

      const visaClean = createVisa || "";
      const nowEpoch = Math.floor(Date.now() / 1000);

      const docData = {
        name: newMemberName,
        country: createCountry || "",
        birthDate: birthEpoch,
        batteryDueDate: dueEpoch,
        lastBatteryReplacementDate: lastEpoch,
        visaType: visaClean,
        visaTypeOriginal: visaClean,
        canFinancialTransactions: true,
        tendency: tendencyNum,
        lastUpdatedClient: nowEpoch,
      };

      await addDoc(collection(db, "members"), docData);

      setCurrentName(newMemberName);
      setNewMemberName("");
      setCreateError("");
      setConnectError("");
    } catch (e) {
      console.error("create member failed:", e);
      setCreateError(e?.message || "Failed to create member.");
    } finally {
      setCreating(false);
    }
  }

  const batteryPercentText =
    mainProgress && Number.isFinite(mainProgress.percent)
      ? `${mainProgress.percent}%`
      : "—";

  const batteryStartDisplay = mainProgress
    ? formatMDY(Math.floor(mainProgress.startDate.getTime() / 1000))
    : "-";

  const batteryEndDisplay = mainProgress
    ? formatMDY(Math.floor(mainProgress.endDate.getTime() / 1000))
    : "-";

  const currentFinancial =
    currentMember?.canFinancialTransactions === true ? "YES" : "NO";

  return (
    <div className="appShell">
      {/* 상단 패널 */}
      <div className="panel panel-main">
        <div className="panelHeader">
          <div>
            <div className="systemTitle">BMD</div>
            <div className="systemSubtitle">
              Battery Management Division / Member Access
            </div>
          </div>
          <div className="panelHeaderStatus">{todayStr}</div>
        </div>

        <div className="panelBody-split">
          {/* 좌측: 이름 입력 */}
          <div className="sectionCard">
            <h3 className="sectionTitle">Identity Check</h3>

            <form className="identityForm" onSubmit={handleConnect}>
              <div className="field">
                <span>First Name</span>
                <input
                  className="input"
                  value={firstNameInput}
                  onChange={(e) => setFirstNameInput(e.target.value)}
                />
              </div>
              <div className="field">
                <span>Last Name</span>
                <input
                  className="input"
                  value={lastNameInput}
                  onChange={(e) => setLastNameInput(e.target.value)}
                />
              </div>

              <div className="identityActions">
                <button
                  type="submit"
                  className="btn"
                  disabled={!membersLoaded}
                >
                  Access Record
                </button>
                <button
                  type="button"
                  className="btn dangerBtn"
                  onClick={handleDeleteRecord}
                  disabled={!membersLoaded}
                >
                  Delete Record
                </button>
              </div>
            </form>

            <div className="statusStack">
              {connectError && (
                <div className="statusText error">{connectError}</div>
              )}
            </div>
          </div>

          {/* 우측: 현재 멤버 대시보드 or 신규 등록 */}
          <div className="sectionCard">
            {currentMember ? (
              <div className="dashboardWrapper">
                <div className="memberHeaderRow">
                  <div>
                    <div className="memberHeaderLabel">Member File</div>
                    <div className="memberName">{currentMember.name}</div>
                  </div>
                </div>

                <div className="batterySection">
                  <div className="batteryHeaderRow">
                    <div>Battery Remaining (0–100)</div>
                    <div>Current Level</div>
                  </div>

                  <div className="batteryBarShell">
                    <div
                      className="batteryBarFill"
                      style={{
                        width:
                          mainProgress &&
                          Number.isFinite(mainProgress.percent)
                            ? `${mainProgress.percent}%`
                            : "0%",
                      }}
                    />
                  </div>

                  <div className="batteryFooterRow">
                    <div>
                      <div className="batteryLabel">
                        Last Replacement
                      </div>
                      <div>{batteryStartDisplay}</div>
                    </div>
                    <div className="batteryRight">
                      <div className="batteryPercent">
                        {batteryPercentText}
                      </div>
                      <div className="batteryLabel">
                        Due {batteryEndDisplay}
                      </div>
                    </div>
                  </div>
                </div>

                <div className="infoGrid">
                  <div className="infoCell">
                    <div className="infoLabel">Country</div>
                    <div className="infoValue">
                      {currentMember.country || "—"}
                    </div>
                  </div>
                  <div className="infoCell">
                    <div className="infoLabel">Birth Date</div>
                    <div className="infoValue">
                      {formatMDY(currentMember.birthDate)}
                    </div>
                  </div>
                  <div className="infoCell">
                    <div className="infoLabel">Battery Due Date</div>
                    <div className="infoValue">
                      {formatMDY(currentMember.batteryDueDate)}
                    </div>
                  </div>
                  <div className="infoCell">
                    <div className="infoLabel">Financial Access</div>
                    <div className="infoValue">{currentFinancial}</div>
                  </div>
                  <div className="infoCell">
                    <div className="infoLabel">
                      Last Battery Replacement
                    </div>
                    <div className="infoValue">
                      {formatMDY(currentMember.lastBatteryReplacementDate)}
                    </div>
                  </div>
                  <div className="infoCell">
                    <div className="infoLabel">Visa Type</div>
                    <div className="infoValue">
                      {currentMember.visaType || "—"}
                    </div>
                  </div>
                  <div className="infoCell">
                    <div className="infoLabel">Tendency</div>
                    <div className="infoValue">
                      {Number.isFinite(currentMember.tendency)
                        ? `${currentMember.tendency}/10`
                        : "—"}
                    </div>
                  </div>
                </div>
              </div>
            ) : newMemberName ? (
              <>
                <h3 className="sectionTitle">
                  New Client Registration — {newMemberName}
                </h3>

                <form
                  onSubmit={handleCreateMember}
                  className="newClientForm"
                >
                  <div className="field">
                    <span>Country</span>
                    <input
                      className="input"
                      value={createCountry}
                      onChange={(e) => setCreateCountry(e.target.value)}
                    />
                  </div>
                  <div className="field">
                    <span>Birth Date (MM/DD/YYYY)</span>
                    <input
                      className="input"
                      value={createBirth}
                      onChange={(e) => setCreateBirth(e.target.value)}
                      placeholder="10/25/1997"
                    />
                  </div>
                  <div className="field">
                    <span>Last Replacement (MM/DD/YYYY)</span>
                    <input
                      className="input"
                      value={createLastReplacement}
                      onChange={(e) =>
                        setCreateLastReplacement(e.target.value)
                      }
                      placeholder="12/11/2025"
                    />
                  </div>
                  <div className="field">
                    <span>Visa Type</span>
                    <input
                      className="input"
                      value={createVisa}
                      onChange={(e) => setCreateVisa(e.target.value)}
                      placeholder="F-1 / H-1B / etc."
                    />
                  </div>
                  <div className="field">
                    <span>Tendency (0–10)</span>
                    <input
                      className="input"
                      value={createTendency}
                      onChange={(e) => setCreateTendency(e.target.value)}
                      placeholder="9"
                    />
                  </div>
                  <div className="newClientSubmitCell">
                    <button
                      type="submit"
                      className="btn"
                      disabled={creating}
                    >
                      {creating ? "Creating..." : "Create Record"}
                    </button>
                  </div>
                </form>

                <div className="statusStack">
                  {createError && (
                    <div className="statusText error">{createError}</div>
                  )}
                </div>
              </>
            ) : null}
          </div>
        </div>
      </div>

      {/* 하단: 다른 사용자 카드 그리드 */}
      <div className="panel panel-others">
        <div className="othersGridWrapper">
          <div className="othersGrid">
            {otherMembers.map((m) => {
              const p = computeLifetimeProgress(m);
              const percent =
                p && Number.isFinite(p.percent) ? p.percent : null;
              const percentText =
                percent === null ? "—" : `${percent}%`;

              const lastDisplay = formatMDY(
                m.lastBatteryReplacementDate
              );
              const dueDisplay = formatMDY(m.batteryDueDate);

              return (
                <div className="otherCard" key={m.id}>
                  <div className="otherTopRow">
                    <div>{m.name}</div>
                    <div>{percentText}</div>
                  </div>
                  <div className="otherBatteryShell">
                    <div
                      className="otherBatteryFill"
                      style={{
                        width:
                          percent === null ? "0%" : `${percent}%`,
                      }}
                    />
                  </div>
                  <div className="otherBottomRow">
                    <span>Last: {lastDisplay}</span>
                    <span>Due: {dueDisplay}</span>
                  </div>
                </div>
              );
            })}

            {otherMembers.length === 0 && (
              <div className="otherCard">
                <div>No additional records.</div>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}