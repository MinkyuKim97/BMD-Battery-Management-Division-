
import "../style.css";
import { useEffect, useMemo, useState } from "react";
// Firestore database API key management
// => .env contains API keys
// Github repo contains '.env.example'
// replace '.env.example' contents with your Firestore database API keys
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

// Year offset setting only for displaying
const DISPLAY_YEAR_OFFSET = 100;

// Full name trimmer
// Since, user is importing their name with two input fields(first and alst name)
// get the full name by combining and trimming them
// And why separate? Make sure user to use right form of name input
function buildFullName(first, last) {
  const f = (first || "").trim();
  const l = (last || "").trim();
  if (!f && !l) return "";
  if (!l) return f;
  if (!f) return l;
  return `${f} ${l}`;
}

// Normalize name for database comparison
function normalizeName(name) {
  return (name || "").trim().replace(/\s+/g, " ").toLowerCase();
}

// For returning right date form from various input shapes
// Null, undefined, Firestore Timestamp, second, millis, string second, and wrong type(just random string)
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

// Applying year offest(100 years)
function applyYearOffset(date, offsetYears = DISPLAY_YEAR_OFFSET) {
  if (!date) return null;
  const d = new Date(date.getTime());
  d.setFullYear(d.getFullYear() + offsetYears);
  return d;
}

// Converting user date input into Unix
// EX. 12/12/2025 -> 1765689600
// The main reason why I decide to use Unix shape is
// for mapping the 'lastreplacementdate' and 'batteryduedate' to
// show the currentbattery percentage
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

// Converting Unix into MM/DD/YYYY shape
function formatMDY(value) {
  const base = epochToDate(value);
  if (!base) return "-";
  const d = applyYearOffset(base);
  const mm = String(d.getMonth() + 1).padStart(2, "0");
  const dd = String(d.getDate()).padStart(2, "0");
  const yyyy = d.getFullYear();
  return `${mm}/${dd}/${yyyy}`;
}

// Calculating the battery percentage
// Start = lastReplacementDate
// End = batteryDueDate
// Now = current time
// Always use the date without 100 year offset
function calPercentage(member) {
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

// Use Tendency to calculate the due date
// Higher tendency means, citizen uses the battery more than usual,
// so, it supposes to use the battery faster.
// So, if the tendency set as 0, due date is lastReplacement + 2 months
// If the tendency set as 10, due date is lastReplacement + 1 month
// In between, linearly interpolate the due date
function calDueDateWithTendency(lastEpoch, tendencyRaw) {
  if (lastEpoch == null) return null;
  const lastDate = new Date(lastEpoch * 1000);
  if (Number.isNaN(lastDate.getTime())) return null;

  let t = Number(tendencyRaw);
  if (!Number.isFinite(t)) t = 0;
  if (t < 0) t = 0;
  if (t > 10) t = 10;
  const norm = t / 10; // 0~1

  // base1 -> +1 month
  // base2 -> +2 month
  const base1 = new Date(lastDate.getTime());
  base1.setMonth(base1.getMonth() + 1);
  base1.setHours(0, 0, 0, 0);

  const base2 = new Date(lastDate.getTime());
  base2.setMonth(base2.getMonth() + 2);
  base2.setHours(0, 0, 0, 0);

  const ms1 = base1.getTime();
  const ms2 = base2.getTime();

  // Based on Tendency, getting closer to base1 number
  const targetMs = ms2 - norm * (ms2 - ms1);

  return Math.floor(targetMs / 1000);
}

// ----------------------------------------------------------------
// ----------------------------------------------------------------
// ----------------------------------------------------------------


// Rendering sections
export function App() {
  // For getting user input
  const [firstNameInput, setFirstNameInput] = useState("");
  const [lastNameInput, setLastNameInput] = useState("");
  const [currentName, setCurrentName] = useState("");
  const [newMemberName, setNewMemberName] = useState("");
  const [connectError, setConnectError] = useState("");
  const [members, setMembers] = useState([]);
  const [membersLoaded, setMembersLoaded] = useState(false);

  // For showing the data from database
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

  // Get current date, but apply 100 years offset right away
  const todayStr = useMemo(() => {
    const base = new Date();
    const d = applyYearOffset(base);
    const mm = String(d.getMonth() + 1).padStart(2, "0");
    const dd = String(d.getDate()).padStart(2, "0");
    const yyyy = d.getFullYear();
    return `${mm}/${dd}/${yyyy}`;
  }, []);

  // Firestore database, information update read
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
    () => calPercentage(currentMember),
    [currentMember]
  );

  // [Penalty]
  // This section is for showing the penalty
  // If the battery percentage goes 0%, 
  // rerender user's financial access and visa type
  // as 'none'
  // Since, it's useEffect, when user replace their battery with hardware(facility)
  // , it automatically refresh the status
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

  // Re-rendering BatteryDueDate
  // Since, user will replace their battery through the hardware(facility),
  // the website detect the changes and apply the result on the due date
  useEffect(() => {
    if (!currentMember || !currentMember.id) return;

    const lastEpoch = currentMember.lastBatteryReplacementDate;
    const tendencyVal = currentMember.tendency;

    const recomputed = calDueDateWithTendency(lastEpoch, tendencyVal);
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

  // When user input their name,
  // check is it on DB or not
  // if match true -> show the dashboard
  // if match false -> show the create new info form
  function handleConnect(e) {
    e?.preventDefault?.();
    if (!membersLoaded) {
      return;
    }

    const full = buildFullName(firstNameInput, lastNameInput);
    if (!full) {
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
      setCreateError("");
      setCreateCountry("");
      setCreateBirth("");
      setCreateLastReplacement("");
      setCreateVisa("");
      setCreateTendency("");
    }
  }

  // Delete current user's data
  // In the future, need to connect with user identification 
  // to not allow other user to delete someone else's data
  async function handleDeleteRecord() {
    if (!membersLoaded) return;

    const full = buildFullName(firstNameInput, lastNameInput);
    if (!full) {
      setConnectError("");
      return;
    }

    const target = normalizeName(full);
    const match = members.find((m) => normalizeName(m.name) === target) || null;
    if (!match) {
      setConnectError(``);
      return;
    }

    const ok = window.confirm(
      `[WARNING] Delete record for "${match.name}"?.`
    );
    if (!ok) return;

    try {
      await deleteDoc(doc(db, "members", match.id));
      if (normalizeName(currentName) === normalizeName(match.name)) {
        setCurrentName("");
      }
      setNewMemberName("");
      // setConnectError(`"${match.name}"`);
      setCreateError("");
    } catch (e) {
      console.error("delete member failed:", e);
      // setConnectError(e?.message || "Failed to delete member.");
    }
  }

  // Creating new data record
  async function handleCreateMember(e) {
    e?.preventDefault?.();
    if (!newMemberName) return;

    setCreating(true);
    setCreateError("");

    try {
      const birthEpoch = parseMDYToEpoch(createBirth);
      if (birthEpoch == null) {
        setCreateError("Birth Date Form -> MM/DD/YYYY");
        setCreating(false);
        return;
      }

      const lastEpoch = parseMDYToEpoch(createLastReplacement);
      if (lastEpoch == null) {
        setCreateError("Last Replacement Form -> MM/DD/YYYY");
        setCreating(false);
        return;
      }

      let tendencyNum = parseInt(createTendency, 10);
      if (!Number.isFinite(tendencyNum)) tendencyNum = 0;
      if (tendencyNum < 0) tendencyNum = 0;
      if (tendencyNum > 10) tendencyNum = 10;

      const dueEpoch = calDueDateWithTendency(lastEpoch, tendencyNum);
      if (dueEpoch == null) {
        setCreateError("Fail calculating Battery due date");
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
      setCreateError(e?.message || "Failed to create data.");
    } finally {
      setCreating(false);
    }
  }

  // Hows battery UI
  // Starts, ends, and percentage
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


  // ----------------------------------------------------------------
  // ----------------------------------------------------------------
  // Actual web render section
  return (
    <div className="totalDiv">      
      <div className="panel panel-main">
        <div className="panelHeader">
          <div>
            <div className="titleTotalBox">
            <div className="titleBig">BMD</div>
            <div className="titleSmall">
              [ Battery Management Division ]
            </div>

            </div>
          </div>
          <div className="panelHeaderStatus">Date:
            <div className="datePlacer">{todayStr}
              </div> </div>
        </div>

        <div className="panelBody-split">
          <div className="sectionCard">
            <h3 className="sectionTitle">Citizen Identity Check</h3>

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
                  Access Data
                </button>
                <button
                  type="button"
                  className="btn dangerBtn"
                  onClick={handleDeleteRecord}
                  disabled={!membersLoaded}
                >
                  Delete Data
                </button>
              </div>
            </form>

            <div className="statusStack">
              {connectError && (
                <div className="statusText error">{connectError}</div>
              )}
            </div>
          </div>


          <div className="sectionCard">
            {currentMember ? (
              <div className="dashboardWrapper">
                <div className="memberHeaderRow">
                  <div>
                    <div className="memberHeaderLabel">Citizen Name:</div>
                    <div className="memberName">{currentMember.name}</div>
                  </div>
                </div>

                <div className="batterySection">
                  <div className="batteryHeaderRow">
                    <div>Battery Percentage:</div>
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
                      <div className="batteryPercent">
                        {batteryPercentText}
                      </div>
                    </div>
                    <div className="batteryRight">
                      <div className="batteryLabel">
                        Last Replacement Date:
                      </div>
                      <div>{batteryStartDisplay}</div>
                      <div className="batteryLabel">
                        Estimated Next Replacement Date:
                      </div>
                      <div>{batteryEndDisplay}</div>
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
                        ? `${currentMember.tendency} / 10`
                        : "—"}
                    </div>
                  </div>
                </div>
              </div>
            ) : newMemberName ? (
              // Creating new citizen data
              <>
                <h3 className="sectionTitle">
                  New Citizen Registration: {newMemberName}
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
                    <span>Birth Date</span>
                    <input
                      className="input"
                      value={createBirth}
                      onChange={(e) => setCreateBirth(e.target.value)}
                      placeholder="MM/DD/YYYY"
                    />
                  </div>
                  <div className="field">
                    <span>Last Battery Replacement Date</span>
                    <input
                      className="input"
                      value={createLastReplacement}
                      onChange={(e) =>
                        setCreateLastReplacement(e.target.value)
                      }
                      placeholder="MM/DD/YYYY"
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
                    <span>Tendency (how hard you work?)</span>
                    <input
                      className="input"
                      value={createTendency}
                      onChange={(e) => setCreateTendency(e.target.value)}
                      placeholder="0 - 10"
                    />
                  </div>
                  <div className="newClientSubmitCell">
                    <button
                      type="submit"
                      className="btn"
                      disabled={creating}
                    >
                      {creating ? "Creating..." : "Create Data"}
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

      {/* Other citizen's battery infos */}
      <div className="panel panel-others">
        <div className="othersGridWrapper">
          <div className="othersGrid">
            {otherMembers.map((m) => {
              const p = calPercentage(m);
              const percent =
                p && Number.isFinite(p.percent) ? p.percent : null;
              const percentText =
                percent === null ? "—" : `${percent}%`;

              const lastDisplay = formatMDY(
                m.lastBatteryReplacementDate
              );
              const tendencyDisplay = m.tendency;
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
                    <span>Tendency: {tendencyDisplay} / 10</span>
                    <span> Battery Due: {dueDisplay}</span>
                  </div>
                </div>
              );
            })}

            {otherMembers.length === 0 && (
              <div className="otherCard">
                <div>No other data</div>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}