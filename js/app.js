const UUID = {
  service: "19b10000-e8f2-537e-4f6c-d104768a1214",
  data: "19b10001-e8f2-537e-4f6c-d104768a1214",
  command: "19b10002-e8f2-537e-4f6c-d104768a1214",
  status: "19b10003-e8f2-537e-4f6c-d104768a1214"
};

const STATUS = {
  OK: 0,
  CAL_START: 1,
  CAL_FAIL: 2,
  CAL_SUCCESS: 3,
  REC_START: 4,
  REC_STOP: 5,
  DISCONNECTED: 6,
  OVERFLOW: 7,
  SENSOR: 8,
  CAL_LOAD_FAIL: 9
};

let state = {
  type: "jump",
  recording: false,
  connected: false,
  device: null,
  server: null,
  dataChar: null,
  cmdChar: null,
  statusChar: null,
  startTime: 0,
  elapsedTimer: null,
  samples: [],
  calibrated: true,
  latestResult: null
};

const $ = id => document.getElementById(id);
const dbKey = "sport_tracker_sessions_v1";

document.querySelectorAll(".nav").forEach(b =>
  b.onclick = () => showPage(b.dataset.page)
);




$("weight").addEventListener("input", () => {

  if (
    state.type === "barbell" &&
    state.samples.length >= 20
  ) {

    const result = calculateMetrics();

    state.latestResult = result;

    setLatest(result);
  }

});


$("activitySelect").addEventListener("change", () => {

  const select = $("activitySelect");
  const custom = $("customActivity");
  const saveButton = $("saveSession");


  // ----------------------------------------------------------
  // SAVE BUTTON / CUSTOM ACTIVITY
  // ----------------------------------------------------------

  if (select.value === "other") {

    custom.classList.remove("hidden");
    custom.focus();

    saveButton.textContent =
      "Save session — Other";

  } else {

    custom.classList.add("hidden");

    const name =
      select.options[select.selectedIndex].text;

    saveButton.textContent =
      `Save session — ${name}`;
  }


  // ----------------------------------------------------------
  // RECALCULATE AND UPDATE DISPLAY
  // ----------------------------------------------------------

  if (
    state.samples &&
    state.samples.length >= 20
  ) {

    const result =
      calculateMetrics();

    state.latestResult =
      result;

    setLatest(result);
  }

});



function showPage(id) {
  document.querySelectorAll(".page").forEach(p =>
    p.classList.toggle("active", p.id === id)
  );

  document.querySelectorAll(".nav").forEach(b =>
    b.classList.toggle("selected", b.dataset.page === id)
  );

  const sub = {
    recordPage: "Capture a new movement",
    logPage: "Track performance over time",
    devicePage: "Bluetooth Low Energy connection",
    settingsPage: "Tracker preferences"
  };

  $("pageSubtitle").textContent = sub[id];

  if (id === "logPage") {
    renderHistory();
  }
}

document.querySelectorAll(".segment").forEach(b =>
  b.onclick = () => {

    document.querySelectorAll(".segment").forEach(x =>
      x.classList.remove("selected")
    );

    b.classList.add("selected");

    // Change activity type
    state.type = b.dataset.type;

    // Show weight only for barbell
    $("weightWrap").classList.toggle(
      "hidden",
      state.type !== "barbell"
    );

    // If we already have a completed recording,
    // recalculate it immediately using the new activity type.
    if (
      state.samples &&
      state.samples.length >= 20
    ) {

      const result = calculateMetrics();

      state.latestResult = result;

      setLatest(result);
    }

  }
);

$("startStop").onclick = () =>
  state.recording ? stopRecording() : startRecording();

$("discard").onclick = discardRecording;

$("saveSession").onclick = () => {

  if (!state.latestResult) {
    toast("No completed recording to save.");
    return;
  }

  const select = $("activitySelect");

  // First click opens the activity selector.
  if (select.classList.contains("hidden")) {
    select.classList.remove("hidden");
    $("customActivity").classList.add("hidden");

    select.focus();
    return;
  }

  saveSession(state.latestResult);
};

$("saveSession").disabled = true;
$("activitySelect").classList.add("hidden");
$("customActivity").classList.add("hidden");

$("saveSession").textContent =
  "Save session — Jump";

$("activitySelect").value = "jump";
$("customActivity").value = "";



$("calibrate").onclick = calibrate;
$("connect").onclick = connectBLE;

$("clearData").onclick = async () => {
  if (confirm("Delete all locally stored sessions?")) {
    localStorage.removeItem(dbKey);
    renderHistory();
    toast("Local sessions cleared");
  }
};


async function connectBLE() {
  try {
    console.log("Starting BLE scan...");

    const device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [UUID.service]
    });

    console.log("FOUND DEVICE:", device.name, device.id);

    device.addEventListener("gattserverdisconnected", () => {
      console.log("GATT disconnected");

      state.connected = false;

      updateConnectionUI();
    });

    console.log("Connecting GATT...");

    const server = await device.gatt.connect();

    console.log("GATT connected:", server);

    console.log("Getting service...");

    const service = await server.getPrimaryService(UUID.service);

    console.log("SERVICE FOUND:", service);

    console.log("Getting characteristics...");

    const dataChar = await service.getCharacteristic(UUID.data);

    console.log("DATA:", dataChar);

    const cmdChar = await service.getCharacteristic(UUID.command);

    console.log("COMMAND:", cmdChar);

    const statusChar = await service.getCharacteristic(UUID.status);

    console.log("STATUS:", statusChar);

    state.device = device;
    state.server = server;
    state.dataChar = dataChar;
    state.cmdChar = cmdChar;
    state.statusChar = statusChar;

    dataChar.addEventListener(
      "characteristicvaluechanged",
      onData
    );

    await dataChar.startNotifications();

    console.log("DATA NOTIFICATIONS STARTED");

    statusChar.addEventListener(
      "characteristicvaluechanged",
      onStatus
    );

    await statusChar.startNotifications();

    state.connected = true;

    updateConnectionUI();

    console.log("===== BLE CONNECTED =====");

    toast("IMU_Sense connected");

  } catch (e) {
    console.error("===== BLE ERROR =====");
    console.error("Name:", e.name);
    console.error("Message:", e.message);
    console.error(e);

    toast(`${e.name}: ${e.message}`);
  }
}


function updateConnectionUI() {
  $("connectionPill").textContent =
    state.connected ? "● Connected" : "● Disconnected";

  $("connectionPill").className =
    "pill " + (state.connected ? "connected" : "disconnected");

  $("deviceTitle").textContent =
    state.connected
      ? (state.device?.name || "IMU_Sense")
      : "No device connected";

  $("deviceText").textContent =
    state.connected
      ? "BLE data link is ready for recording."
      : "Connect your IMU device before recording.";

  $("connect").textContent =
    state.connected
      ? "Connected"
      : "Connect to IMU_Sense";
}


async function sendCommand(cmd) {
  if (!state.cmdChar) {
    toast("Connect the IMU first.");
    return false;
  }

  try {
    await state.cmdChar.writeValueWithoutResponse(
      new TextEncoder().encode(cmd)
    );

    return true;

  } catch (e) {
    console.error(e);
    toast("Could not send command.");
    return false;
  }
}


function onStatus(ev) {
  const code = ev.target.value.getUint8(0);

  if (code === STATUS.CAL_LOAD_FAIL) {
    // The device could not load its saved calibration on startup.
    // User must perform calibration before recording.
    state.calibrated = false;

    $("recordStatus").textContent = "Calibration required";
    $("startStop").disabled = true;

    toast("Calibration required before recording.");
    return;
  }

  if (code === STATUS.CAL_START) {
    // Manual calibration has started.
    state.calibrated = false;

    $("recordStatus").textContent = "Calibrating…";
    $("startStop").disabled = true;

    return;
  }

  if (code === STATUS.CAL_SUCCESS) {
    // Manual calibration completed successfully.
    state.calibrated = true;

    $("recordStatus").textContent = "Ready";
    $("startStop").disabled = false;

    toast("Calibration successful");
    return;
  }

  if (code === STATUS.CAL_FAIL) {
    // Manual calibration actually failed.
    state.calibrated = false;

    $("recordStatus").textContent = "Calibration failed";
    $("startStop").disabled = true;

    toast("Calibration failed.");
    return;
  }

  if (code === STATUS.REC_START) {
    toast("Recording started");
    return;
  }

  if (code === STATUS.REC_STOP) {
    toast("Recording stopped");
    return;
  }

  if (code === STATUS.OVERFLOW) {
    discardRecording();
    toast("Device buffer overflow.");
    return;
  }

  if (code === STATUS.SENSOR) {
    state.calibrated = false;
    $("startStop").disabled = true;

    toast("Sensor error.");
    return;
  }
}


function onData(ev) {
  const v = ev.target.value;

  for (let o = 0; o + 10 <= v.byteLength; o += 10) {
    state.samples.push({
      t: v.getUint32(o, true),
      x: v.getInt16(o + 4, true),
      y: v.getInt16(o + 6, true),
      z: v.getInt16(o + 8, true)
    });
  }

  console.log("IMU samples:", state.samples.length);

  // Redraw immediately when new data arrives.
  if (!state.recording) {
    drawLive();
  }
}


async function startRecording() {
  if (!state.connected) {
    toast("Connect the IMU first.");
    return;
  }

  if (!state.calibrated) {
    toast("Calibrate the IMU before recording.");
    return;
  }

  // Clear previous recording.
  state.samples = [];

  // A new recording means there is currently
  // no unsaved result.
  state.latestResult = null;

  $("saveSession").disabled = true;

  state.recording = true;
  state.startTime = performance.now();

  $("recordStatus").textContent = "Recording";

  $("startStop").textContent = "■ Stop recording";
  $("startStop").classList.add("recording");

  $("elapsed").textContent = "00:00.0";

  state.elapsedTimer = setInterval(updateElapsed, 100);

  await sendCommand("START");

  drawLive();
}


async function stopRecording() {
  state.recording = false;

  clearInterval(state.elapsedTimer);
  state.elapsedTimer = null;

  $("recordStatus").textContent = "Ready";

  $("startStop").textContent = "● Start recording";
  $("startStop").classList.remove("recording");

  await sendCommand("STOP");

  const result = calculateMetrics();
  state.latestResult = result;
  setLatest(result);

  // Store the calculated result so it can be
  // saved later using the Save session button.
  $("saveSession").disabled = !state.samples.length;
  
  // Draw the final complete activity.
  drawLive();
}

function setLatest(r) {

  if (state.type === "jump") {

    $("metric1Label").textContent = "Reps";
    $("metric1").textContent = r.repCount;

    $("metric2Label").textContent = "Peak jump height";
    $("metric2").textContent =
      r.overallPeakJumpHeight.toFixed(2) + " m";

    $("metric3Label").textContent = "Average jump height";
    $("metric3").textContent =
      r.averageJumpHeight.toFixed(2) + " m";

    $("metric4Label").textContent = "Peak velocity";
    $("metric4").textContent =
      r.peakVelocity.toFixed(2) + " m/s";

  } else {

    $("metric1Label").textContent = "Reps";
    $("metric1").textContent = r.repCount;

    $("metric2Label").textContent = "Peak power";
    $("metric2").textContent =
      Math.round(r.overallPeakPower) + " W";

    $("metric3Label").textContent = "Peak velocity";
    $("metric3").textContent =
      r.peakVelocity.toFixed(2) + " m/s";

    $("metric4Label").textContent = "Average peak power";
    $("metric4").textContent =
      Math.round(r.averagePeakPower) + " W";
  }
}


function discardRecording() {
  state.recording = false;

  clearInterval(state.elapsedTimer);
  state.elapsedTimer = null;

  state.samples = [];
  state.latestResult = null;

  $("recordStatus").textContent = "Ready";

  $("startStop").textContent = "● Start recording";
  $("startStop").classList.remove("recording");

  $("elapsed").textContent = "00:00.0";

  $("saveSession").disabled = true;

  drawLive();

  toast("Recording discarded");
}


async function calibrate() {
  if (!state.connected) {
    toast("Connect the IMU first.");
    return;
  }

  if (state.recording) {
    toast("Stop recording before calibrating.");
    return;
  }

  // Calibration is now running.
  state.calibrated = false;

  $("recordStatus").textContent = "Calibrating…";

  // Disable recording and calibration buttons.
  $("startStop").disabled = true;
  $("calibrate").disabled = true;

  // Start spinning the gear.
  $("calibrateIcon").classList.add("spinning");

  $("calibrateText").textContent = "Calibrating…";

  const sent = await sendCommand("CALIBRATE");

  // If the command itself failed to send,
  // don't leave the UI stuck in calibration mode.
  if (!sent) {
    state.calibrated = false;

    $("recordStatus").textContent = "Ready";

    $("startStop").disabled = false;
    $("calibrate").disabled = false;

    $("calibrateIcon").classList.remove("spinning");

    $("calibrateText").textContent = "Calibrate";
  }
}


function updateElapsed() {
  const s = (performance.now() - state.startTime) / 1000;

  $("elapsed").textContent =
    `${String(Math.floor(s / 60)).padStart(2, "0")}:` +
    `${(s % 60).toFixed(1).padStart(4, "0")}`;
}

function lowPassAcceleration(samples, cutoffHz = 20) {
  const RC = 1 / (2 * Math.PI * cutoffHz);

  const filtered = new Array(samples.length);

  if (!samples.length) {
    return filtered;
  }

  filtered[0] = samples[0].z / 1000;

  for (let i = 1; i < samples.length; i++) {

    const dt = Math.max(
      0.0005,
      (samples[i].t - samples[i - 1].t) / 1e6
    );

    const raw =
      samples[i].z / 1000;

    const alpha =
      dt / (RC + dt);

    filtered[i] =
      filtered[i - 1] +
      alpha * (raw - filtered[i - 1]);
  }

  return filtered;
}
function calculateMetrics() {

  if (state.samples.length < 20) {
    return {
      peakVelocity: 0,
      peakPower: 0,
      peakHeight: 0,
      reps: [],
      repCount: 0,
      averagePeakPower: 0,
      overallPeakPower: 0,
      averageJumpHeight: 0,
      overallPeakJumpHeight: 0
    };
  }

  const samples = state.samples;

  // ----------------------------------------------------------
  // Activity
  // ----------------------------------------------------------

  const activity =
    $("activitySelect")?.value || state.type;

  const isJump =
    activity.toLowerCase() === "jump";

  // ----------------------------------------------------------
  // Filter Z acceleration
  // ----------------------------------------------------------

  const filtered =
    lowPassAcceleration(samples, 20);


  // ----------------------------------------------------------
  // Detect reps
  // ----------------------------------------------------------

  const reps =
    detectReps(samples, filtered);


  // ----------------------------------------------------------
  // Calculate velocity over the complete recording
  // ----------------------------------------------------------

  const velocities =
    new Array(samples.length);

  velocities[0] = 0;

  let velocity = 0;

  for (let i = 1; i < samples.length; i++) {

    const dt =
      Math.max(
        0.0005,
        (samples[i].t - samples[i - 1].t) / 1e6
      );

    const acceleration =
      filtered[i] * 9.80665;

    velocity +=
      acceleration * dt;

    velocities[i] = velocity;
  }


  // ----------------------------------------------------------
  // Overall velocity
  // ----------------------------------------------------------

  let overallPeakVelocity = 0;

  for (let i = 0; i < velocities.length; i++) {

    if (isJump) {
      // For jumps we care about upward velocity.
      overallPeakVelocity =
        Math.max(
          overallPeakVelocity,
          velocities[i]
        );
    } else {
      // For general movements, magnitude is more useful.
      overallPeakVelocity =
        Math.max(
          overallPeakVelocity,
          Math.abs(velocities[i])
        );
    }
  }


  // ----------------------------------------------------------
  // Mass
  // ----------------------------------------------------------

  const mass =
    state.type === "barbell"
      ? Math.max(
          0,
          Number($("weight")?.value) || 0
        )
      : 0;


  // ----------------------------------------------------------
  // Calculate each rep
  // ----------------------------------------------------------

  const repResults = [];

  for (const rep of reps) {

    let peakVelocity = 0;
    let peakAcceleration = 0;
    let peakIndex = rep.start;

    for (
      let i = rep.start;
      i <= rep.end;
      i++
    ) {

      const currentVelocity =
        isJump
          ? velocities[i]
          : Math.abs(velocities[i]);

      if (
        currentVelocity >
        peakVelocity
      ) {
        peakVelocity =
          currentVelocity;

        peakIndex = i;
      }

      peakAcceleration =
        Math.max(
          peakAcceleration,
          Math.abs(
            filtered[i] * 9.80665
          )
        );
    }


    // --------------------------------------------------------
    // Peak power
    // --------------------------------------------------------

    let peakPower = 0;

    if (mass > 0) {

      peakPower =
        mass *
        (
          peakAcceleration + 9.80665
        ) *
        peakVelocity;
    }


    // --------------------------------------------------------
    // Jump height
    //
    // ONLY calculated for Jump.
    // --------------------------------------------------------

    let jumpHeight = 0;

    if (isJump) {

      jumpHeight =
        (
          peakVelocity *
          peakVelocity
        ) /
        (2 * 9.80665);
    }


    repResults.push({
      start: rep.start,
      end: rep.end,
      peakIndex,
      peakVelocity,
      peakPower,
      jumpHeight
    });
  }


  // ----------------------------------------------------------
  // Aggregate results
  // ----------------------------------------------------------

  const powerValues =
    repResults
      .map(r => r.peakPower)
      .filter(v => v > 0);

  const jumpValues =
    isJump
      ? repResults
          .map(r => r.jumpHeight)
          .filter(v => v > 0)
      : [];


  const averagePeakPower =
    powerValues.length
      ? powerValues.reduce(
          (a, b) => a + b,
          0
        ) / powerValues.length
      : 0;


  const overallPeakPower =
    powerValues.length
      ? Math.max(...powerValues)
      : 0;


  const averageJumpHeight =
    jumpValues.length
      ? jumpValues.reduce(
          (a, b) => a + b,
          0
        ) / jumpValues.length
      : 0;


  const overallPeakJumpHeight =
    jumpValues.length
      ? Math.max(...jumpValues)
      : 0;


  return {

    peakVelocity:
      overallPeakVelocity,

    // Keep this for compatibility
    // with your existing UI.
    peakPower:
      overallPeakPower,

    // Only non-zero for Jump.
    peakHeight:
      overallPeakJumpHeight,

    reps:
      repResults,

    repCount:
      repResults.length,

    averagePeakPower,

    overallPeakPower,

    averageJumpHeight,

    overallPeakJumpHeight
  };
}


// ============================================================
// GENERAL REP DETECTOR
// ============================================================
function detectReps(samples, filtered) {

  const reps = [];

  if (!samples || samples.length < 20) {
    return reps;
  }

  // ----------------------------------------------------------
  // THRESHOLDS
  // ----------------------------------------------------------

  // Movement must get above this level.
  const START_THRESHOLD = 0.15; // g

  // Movement must fall below this level.
  const END_THRESHOLD = 0.10; // g

  // Signal must remain ABOVE the start threshold
  // for this long before a rep starts.
  const START_CONFIRM_TIME = 50; // ms

  // Signal must remain BELOW the end threshold
  // for this long before a rep ends.
  const END_CONFIRM_TIME = 50; // ms

  // Minimum total rep duration.
  const MIN_REP_TIME = 400; // ms

  // Minimum time between completed reps.
  const MIN_REST_TIME = 150; // ms


  // ----------------------------------------------------------
  // STATE
  // ----------------------------------------------------------

  let inRep = false;

  let startIndex = 0;

  let aboveStartSince = null;

  let belowEndSince = null;

  let lastRepEndTime = -Infinity;


  // ----------------------------------------------------------
  // PROCESS SIGNAL
  // ----------------------------------------------------------

  for (
    let i = 1;
    i < samples.length;
    i++
  ) {

    const time =
      samples[i].t / 1000;

    const value =
      Math.abs(filtered[i]);


    // ========================================================
    // WAITING FOR REP
    // ========================================================

    if (!inRep) {

      if (value > START_THRESHOLD) {

        // Start timing the threshold crossing.
        if (aboveStartSince === null) {
          aboveStartSince = time;
        }

        const aboveDuration =
          time - aboveStartSince;

        /*
         * The signal has remained above the threshold
         * long enough to count as intentional movement.
         */
        if (
          aboveDuration >= START_CONFIRM_TIME &&
          time - lastRepEndTime >= MIN_REST_TIME
        ) {

          inRep = true;

          /*
           * Go back to the beginning of the
           * threshold crossing rather than starting
           * exactly at the confirmation point.
           */
          startIndex = i;

          belowEndSince = null;
        }

      } else {

        /*
         * Signal dropped back below the threshold
         * before confirmation.
         *
         * Therefore it was probably just noise.
         */
        aboveStartSince = null;
      }

      continue;
    }


    // ========================================================
    // CURRENTLY INSIDE REP
    // ========================================================

    if (value < END_THRESHOLD) {

      if (belowEndSince === null) {
        belowEndSince = time;
      }

      const belowDuration =
        time - belowEndSince;

      const repDuration =
        time -
        samples[startIndex].t / 1000;


      /*
       * Only finish the rep if the signal has stayed
       * quiet for the required amount of time.
       */
      if (
        belowDuration >= END_CONFIRM_TIME &&
        repDuration >= MIN_REP_TIME
      ) {

        reps.push({
          start: startIndex,
          end: i
        });

        inRep = false;

        lastRepEndTime = time;

        aboveStartSince = null;
        belowEndSince = null;
      }

    } else {

      /*
       * Movement came back before the end was confirmed.
       *
       * Therefore the rep is still happening.
       */
      belowEndSince = null;
    }
  }


  // ==========================================================
  // FINAL REP
  // ==========================================================

  if (inRep) {

    const endIndex =
      samples.length - 1;

    const endTime =
      samples[endIndex].t / 1000;

    const startTime =
      samples[startIndex].t / 1000;

    const duration =
      endTime - startTime;

    if (duration >= MIN_REP_TIME) {

      reps.push({
        start: startIndex,
        end: endIndex
      });
    }
  }


  return reps;
}
function drawLive() {
  const c = $("liveChart");

  if (!c) return;

  const ctx = c.getContext("2d");

  const w = c.clientWidth;
  const h = c.clientHeight;

  if (!w || !h) return;

  const dpr = window.devicePixelRatio || 1;

  c.width = w * dpr;
  c.height = h * dpr;

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  ctx.clearRect(0, 0, w, h);

  // ---------------- GRID ----------------

  ctx.strokeStyle = "#252b34";
  ctx.lineWidth = 1;

  for (let i = 1; i < 5; i++) {
    ctx.beginPath();
    ctx.moveTo(0, h * i / 5);
    ctx.lineTo(w, h * i / 5);
    ctx.stroke();
  }

  // ---------------- DATA LINE ----------------

  if (state.samples.length > 1) {

    const arr = state.samples;

    ctx.beginPath();

    ctx.strokeStyle = "#8fbaf0";
    ctx.lineWidth = 2;

    for (let i = 0; i < arr.length; i++) {

      const x =
        i / (arr.length - 1) * w;

      const value =
        arr[i].z / 1000;

      const y =
        h / 2 -
        value * 30;

      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }

    ctx.stroke();


    // ==================================================
    // PEAK DOTS
    // ==================================================
    //
    // Only show these AFTER recording has stopped.
    //
    if (
      !state.recording &&
      state.latestResult &&
      state.latestResult.reps
    ) {

      const reps =
        state.latestResult.reps;

      reps.forEach(rep => {

        /*
         * Find the peak point inside this repetition.
         */
        let peakIndex = rep.start;
        let peakValue =
          Math.abs(
            arr[peakIndex].z / 1000
          );

        for (
          let i = rep.start;
          i <= rep.end;
          i++
        ) {

          const value =
            Math.abs(
              arr[i].z / 1000
            );

          if (value > peakValue) {
            peakValue = value;
            peakIndex = i;
          }
        }

        /*
         * Convert sample index to graph position.
         */
        const x =
          peakIndex /
          (arr.length - 1) *
          w;

        const value =
          arr[peakIndex].z / 1000;

        const y =
          h / 2 -
          value * 30;

        /*
         * Draw peak dot.
         */
        ctx.beginPath();

        ctx.fillStyle = "#e9edf2";

        ctx.arc(
          x,
          y,
          5,
          0,
          Math.PI * 2
        );

        ctx.fill();
      });
    }

  } else {

    ctx.fillStyle = "#69717e";
    ctx.font = "13px system-ui";
    ctx.textAlign = "center";

    ctx.fillText(
      "Waiting for IMU data…",
      w / 2,
      h / 2
    );
  }

  // Keep graph updating during recording.
  if (state.recording) {
    requestAnimationFrame(drawLive);
  }
}

function sessions() {
  try {
    return JSON.parse(
      localStorage.getItem(dbKey) || "[]"
    );
  } catch {
    return [];
  }
}


function saveSession(r) {
  if (!r) {
    toast("No result available to save.");
    return;
  }

  let activity = $("activitySelect").value;

  // If "Other" was selected, use the custom name.
  if (activity === "other") {
    activity = $("customActivity").value.trim();

    if (!activity) {
      toast("Enter an activity name.");
      $("customActivity").focus();
      return;
    }
  }

  // Make activity names consistent.
  // "Squat", "SQUAT", and "squat" become "squat".
  activity = activity
    .trim()
    .toLowerCase()
    .replace(/\s+/g, " ");

  if (!activity) {
    toast("Enter an activity name.");
    return;
  }

  const s = sessions();

  const session = {
  id:
    crypto.randomUUID?.() ||
    String(Date.now()),

  date: new Date().toISOString(),

  activity,

  type: state.type
};


    if (state.type === "jump") {

    session.repCount =
        r.repCount;

    session.averageJumpHeight =
        r.averageJumpHeight;

    session.overallPeakJumpHeight =
        r.overallPeakJumpHeight;

    session.peakVelocity =
        r.peakVelocity;

    } else {

    session.repCount =
        r.repCount;

    session.averagePeakPower =
        r.averagePeakPower;

    session.overallPeakPower =
        r.overallPeakPower;

    session.peakVelocity =
        r.peakVelocity;

    session.weightKg =
        Number($("weight").value) || 0;
    }


    s.push(session);

  localStorage.setItem(
    dbKey,
    JSON.stringify(s)
  );

  renderHistory();

  toast("Session saved locally");

  // Prevent accidentally saving the same recording twice.
  $("saveSession").disabled = true;
}

function renderHistory() {

  const all = sessions();

  const filter =
    $("activityFilter").value || "All";


  // ----------------------------------------------------------
  // NORMALIZE ACTIVITY NAMES
  // ----------------------------------------------------------

  const normalizeActivity = activity =>
    String(activity)
      .trim()
      .toLowerCase()
      .replace(/\s+/g, " ");


  // ----------------------------------------------------------
  // GET UNIQUE ACTIVITIES
  // ----------------------------------------------------------

  const names = [
    ...new Set(
      all.map(s =>
        normalizeActivity(s.activity)
      )
    )
  ];


  // ----------------------------------------------------------
  // REBUILD ACTIVITY DROPDOWN
  // ----------------------------------------------------------

  $("activityFilter").innerHTML =
    "<option value=\"All\">All</option>" +
    names
      .map(n =>
        `<option value="${escapeHtml(n)}">${escapeHtml(
          n.replace(/\b\w/g, c => c.toUpperCase())
        )}</option>`
      )
      .join("");


  // Keep previous selection
  const normalizedFilter =
    filter === "All"
      ? "All"
      : normalizeActivity(filter);


  $("activityFilter").value =
    names.includes(normalizedFilter)
      ? normalizedFilter
      : "All";


  // ----------------------------------------------------------
  // FILTER SESSIONS
  // ----------------------------------------------------------

  const selectedActivity =
    $("activityFilter").value;


  const filtered =
    all.filter(s => {

      if (selectedActivity === "All") {
        return true;
      }

      return (
        normalizeActivity(s.activity) ===
        selectedActivity
      );
    });


  // ----------------------------------------------------------
  // METRIC DROPDOWN
  // ----------------------------------------------------------

  const metric =
    $("metricFilter").value;


  // ----------------------------------------------------------
  // GET VALUES FOR GRAPH
  // ----------------------------------------------------------

  const vals =
    filtered
      .map(s => {

        // -------------------------
        // POWER
        // -------------------------

        if (metric === "Peak Power") {

          return Number(
            s.overallPeakPower
          ) || 0;
        }


        // -------------------------
        // AVERAGE POWER
        // -------------------------

        if (
          metric === "Average Peak Power"
        ) {

          return Number(
            s.averagePeakPower
          ) || 0;
        }


        // -------------------------
        // JUMP HEIGHT
        // -------------------------

        if (
          metric === "Peak Height"
        ) {

          return Number(
            s.overallPeakJumpHeight
          ) || 0;
        }


        // -------------------------
        // AVERAGE JUMP HEIGHT
        // -------------------------

        if (
          metric === "Average Jump Height"
        ) {

          return Number(
            s.averageJumpHeight
          ) || 0;
        }


        // -------------------------
        // VELOCITY
        // -------------------------

        if (
          metric === "Peak Velocity"
        ) {

          return Number(
            s.peakVelocity
          ) || 0;
        }


        return 0;
      });


  // ----------------------------------------------------------
  // DRAW GRAPH
  // ----------------------------------------------------------

  drawHistory(
    vals,
    metric
  );


  // ----------------------------------------------------------
  // DRAW SAVED SESSIONS
  // ----------------------------------------------------------

  $("sessions").innerHTML =
    filtered
      .slice()
      .reverse()
      .map(s => {

        const activityName =
          String(s.activity)
            .trim()
            .replace(
              /\b\w/g,
              c => c.toUpperCase()
            );


        // --------------------------------
        // DISPLAY VALUE
        // --------------------------------

        let value;


        if (
          metric === "Peak Power"
        ) {

          value =
            `${Math.round(
              Number(s.overallPeakPower) || 0
            )} W`;

        }

        else if (
          metric === "Average Peak Power"
        ) {

          value =
            `${Math.round(
              Number(s.averagePeakPower) || 0
            )} W`;

        }

        else if (
          metric === "Peak Height"
        ) {

          value =
            `${(
              Number(
                s.overallPeakJumpHeight
              ) || 0
            ).toFixed(2)} m`;

        }

        else if (
          metric === "Average Jump Height"
        ) {

          value =
            `${(
              Number(
                s.averageJumpHeight
              ) || 0
            ).toFixed(2)} m`;

        }

        else if (
          metric === "Peak Velocity"
        ) {

          value =
            `${(
              Number(
                s.peakVelocity
              ) || 0
            ).toFixed(2)} m/s`;

        }

        else {

          value = "—";
        }


        // --------------------------------
        // SESSION HTML
        // --------------------------------

        return `
          <div class="session">

            <div>

              <div class="session-title">
                ${escapeHtml(activityName)}
              </div>

              <div class="session-date">

                ${new Date(
                  s.date
                ).toLocaleString()}

                ${
                  s.weightKg
                    ? ` · ${s.weightKg} kg`
                    : ""
                }

              </div>

            </div>


            <div class="session-values">

              ${value}

              <small>
                ${escapeHtml(metric)}
              </small>

            </div>

          </div>
        `;
      })
      .join("");
}


$("activityFilter").onchange =
  renderHistory;

$("metricFilter").onchange =
  renderHistory;


function drawHistory(values, metric) {
  const c = $("historyChart");

  if (!c) return;

  const ctx = c.getContext("2d");

  const w = c.clientWidth;
  const h = c.clientHeight;

  if (!w || !h) return;

  const dpr = window.devicePixelRatio || 1;

  c.width = w * dpr;
  c.height = h * dpr;

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  ctx.clearRect(0, 0, w, h);

  ctx.strokeStyle = "#252b34";

  for (let i = 1; i < 5; i++) {
    ctx.beginPath();
    ctx.moveTo(0, h * i / 5);
    ctx.lineTo(w, h * i / 5);
    ctx.stroke();
  }

  if (!values.length) {
    ctx.fillStyle = "#69717e";
    ctx.font = "13px system-ui";
    ctx.textAlign = "center";

    ctx.fillText(
      "Save sessions to build your history",
      w / 2,
      h / 2
    );

    return;
  }

  const max =
    Math.max(...values, 1);

  const min =
    Math.min(...values, 0);

  const range =
    Math.max(max - min, 1);

  ctx.beginPath();

  ctx.strokeStyle = "#8fbaf0";
  ctx.lineWidth = 2.5;

  values.forEach((v, i) => {
    const x =
      values.length === 1
        ? w / 2
        : i / (values.length - 1) * w;

    const y =
      h -
      15 -
      (v - min) / range *
      (h - 30);

    if (i === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  });

  ctx.stroke();

  values.forEach((v, i) => {
    const x =
      values.length === 1
        ? w / 2
        : i / (values.length - 1) * w;

    const y =
      h -
      15 -
      (v - min) / range *
      (h - 30);

    ctx.beginPath();

    ctx.fillStyle = "#e9edf2";

    ctx.arc(
      x,
      y,
      3.5,
      0,
      Math.PI * 2
    );

    ctx.fill();
  });

  ctx.fillStyle = "#69717e";
  ctx.font = "11px system-ui";
  ctx.textAlign = "left";

  ctx.fillText(
    metric,
    8,
    15
  );
}


function escapeHtml(s) {
  return String(s).replace(
    /[&<>"']/g,
    c => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#039;"
    }[c])
  );
}


function toast(msg) {
  const t = $("toast");

  t.textContent = msg;

  t.classList.add("show");

  clearTimeout(toast.timer);

  toast.timer = setTimeout(
    () => t.classList.remove("show"),
    2600
  );
}


window.addEventListener(
  "resize",
  () => {
    if (
      $("recordPage").classList.contains("active")
    ) {
      drawLive();
    }

    if (
      $("logPage").classList.contains("active")
    ) {
      renderHistory();
    }
  }
);


renderHistory();

updateConnectionUI();

drawLive();

// Save should initially be unavailable because
// there is no completed recording yet.
$("saveSession").disabled = true;