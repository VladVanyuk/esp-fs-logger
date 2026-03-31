const state = {
  directory: "/logs/csv",
  selectedFile: "",
  headers: [],
  rows: [],
  delimiter: "\t",
  fileEntries: [],
  status: null,
};

const chartColors = ["#ea7a2f", "#1c8c84", "#4c6edb", "#d5527b"];

function $(id) {
  return document.getElementById(id);
}

function setMessage(text, tone = "info") {
  const message = $("message");
  message.textContent = text;
  message.className = `message ${tone}`;
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  if (!response.ok) {
    throw new Error(await response.text());
  }
  return response.json();
}

async function fetchText(url, options) {
  const response = await fetch(url, options);
  if (!response.ok) {
    throw new Error(await response.text());
  }
  return response.text();
}

function detectDelimiter(text) {
  const firstLine = text.split(/\r?\n/).find((line) => line.trim().length > 0) || "";
  return firstLine.includes("\t") ? "\t" : ",";
}

function parseLog(text) {
  const delimiter = detectDelimiter(text);
  const lines = text
    .split(/\r?\n/)
    .map((line) => line.trimEnd())
    .filter((line) => line.length > 0);

  if (lines.length === 0) {
    return { delimiter, headers: [], rows: [] };
  }

  const rows = lines.map((line) => line.split(delimiter));
  const headers = rows.shift() || [];
  return { delimiter, headers, rows };
}

function escapeHtml(value) {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function renderTable() {
  const wrap = $("table-wrap");

  if (state.headers.length === 0) {
    wrap.innerHTML = '<div class="empty-state">No rows found in this log file.</div>';
    return;
  }

  const headHtml = state.headers
    .map((header) => `<th>${escapeHtml(header)}</th>`)
    .join("");

  const bodyHtml = state.rows
    .map((row) => {
      const cells = state.headers
        .map((_, index) => {
          const cell = row[index] ?? "";
          return `<td contenteditable="true">${escapeHtml(cell)}</td>`;
        })
        .join("");
      return `<tr>${cells}</tr>`;
    })
    .join("");

  wrap.innerHTML = `
    <table>
      <thead><tr>${headHtml}</tr></thead>
      <tbody>${bodyHtml}</tbody>
    </table>
  `;
}

function collectTableText() {
  const table = $("table-wrap").querySelector("table");
  if (!table) {
    return "";
  }

  const rows = Array.from(table.querySelectorAll("tr")).map((row) =>
    Array.from(row.children).map((cell) => cell.innerText.trim())
  );

  return rows.map((row) => row.join(state.delimiter)).join("\n") + "\n";
}

function humanSize(bytes) {
  if (!Number.isFinite(bytes)) {
    return "-";
  }
  if (bytes < 1024) {
    return `${bytes} B`;
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function updateStatus(status) {
  state.status = status;
  $("ap-ssid").textContent = status.apSsid || "Waiting";
  $("ap-ip").textContent = status.apIp || "-";
  $("uart-summary").textContent = `UART${status.uartPort} RX${status.uartRxPin} @ ${status.uartBaud}`;
  $("storage-summary").textContent = `${humanSize(status.storageUsed)} / ${humanSize(status.storageTotal)}`;
}

function renderFileList(entries) {
  const list = $("file-list");
  $("file-count").textContent = String(entries.length);

  if (entries.length === 0) {
    list.innerHTML = '<li class="empty-state">No log files yet.</li>';
    return;
  }

  list.innerHTML = entries
    .map((entry) => {
      const isActive = entry.path === state.selectedFile;
      return `
        <li class="file-item">
          <button type="button" class="${isActive ? "active" : ""}" data-path="${entry.path}">
            <div>${escapeHtml(entry.name)}</div>
            <div class="file-meta">
              <span>${humanSize(entry.size)}</span>
              <span>${entry.active ? "active" : "archived"}</span>
            </div>
          </button>
        </li>
      `;
    })
    .join("");

  list.querySelectorAll("button[data-path]").forEach((button) => {
    button.addEventListener("click", async () => {
      await loadFile(button.dataset.path);
    });
  });
}

function toNumber(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function buildSeries() {
  const preferredX = state.headers.findIndex((header) => /sys_time|time/i.test(header));
  const xIndex = preferredX >= 0 ? preferredX : -1;

  const numericColumns = state.headers
    .map((header, index) => ({
      header,
      index,
      valid: state.rows.some((row) => toNumber(row[index]) !== null),
    }))
    .filter((column) => column.valid);

  const preferredNames = ["x", "y", "z", "bat", "ToF_signal", "accel", "freefall"];
  numericColumns.sort((left, right) => {
    const leftPref = preferredNames.indexOf(left.header);
    const rightPref = preferredNames.indexOf(right.header);
    if (leftPref === -1 && rightPref === -1) {
      return left.index - right.index;
    }
    if (leftPref === -1) {
      return 1;
    }
    if (rightPref === -1) {
      return -1;
    }
    return leftPref - rightPref;
  });

  const selectedColumns = numericColumns.filter((column) => column.index !== xIndex).slice(0, 4);
  const xValues = state.rows.map((row, index) => {
    if (xIndex >= 0) {
      return toNumber(row[xIndex]) ?? index;
    }
    return index;
  });

  return selectedColumns.map((column, seriesIndex) => ({
    name: column.header,
    color: chartColors[seriesIndex % chartColors.length],
    points: state.rows
      .map((row, rowIndex) => {
        const y = toNumber(row[column.index]);
        return y === null ? null : { x: xValues[rowIndex], y };
      })
      .filter(Boolean),
  }));
}

function drawChart() {
  const canvas = $("chart");
  const ctx = canvas.getContext("2d");
  const legend = $("legend");
  const series = buildSeries();

  legend.innerHTML = "";
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  if (series.length === 0 || series.every((item) => item.points.length === 0)) {
    ctx.fillStyle = "#6a716f";
    ctx.font = "18px Avenir Next";
    ctx.fillText("No numeric columns available for charting.", 32, 48);
    return;
  }

  series.forEach((item) => {
    const chip = document.createElement("div");
    chip.className = "legend-item";
    chip.innerHTML = `<span class="legend-swatch" style="background:${item.color}"></span>${escapeHtml(item.name)}`;
    legend.appendChild(chip);
  });

  const allPoints = series.flatMap((item) => item.points);
  const xMin = Math.min(...allPoints.map((point) => point.x));
  const xMax = Math.max(...allPoints.map((point) => point.x));
  const yMin = Math.min(...allPoints.map((point) => point.y));
  const yMax = Math.max(...allPoints.map((point) => point.y));

  const chartBox = { left: 64, right: canvas.width - 24, top: 20, bottom: canvas.height - 40 };
  const usableWidth = Math.max(1, chartBox.right - chartBox.left);
  const usableHeight = Math.max(1, chartBox.bottom - chartBox.top);
  const xRange = xMax - xMin || 1;
  const yRange = yMax - yMin || 1;

  ctx.strokeStyle = "rgba(18, 33, 38, 0.12)";
  ctx.lineWidth = 1;
  for (let step = 0; step <= 4; step += 1) {
    const y = chartBox.top + (usableHeight / 4) * step;
    ctx.beginPath();
    ctx.moveTo(chartBox.left, y);
    ctx.lineTo(chartBox.right, y);
    ctx.stroke();
  }

  ctx.strokeStyle = "rgba(18, 33, 38, 0.45)";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(chartBox.left, chartBox.top);
  ctx.lineTo(chartBox.left, chartBox.bottom);
  ctx.lineTo(chartBox.right, chartBox.bottom);
  ctx.stroke();

  ctx.fillStyle = "#445056";
  ctx.font = "13px Avenir Next";
  ctx.fillText(yMax.toFixed(2), 14, chartBox.top + 4);
  ctx.fillText(yMin.toFixed(2), 14, chartBox.bottom);
  ctx.fillText(xMin.toFixed(2), chartBox.left, canvas.height - 12);
  ctx.fillText(xMax.toFixed(2), chartBox.right - 40, canvas.height - 12);

  series.forEach((item) => {
    if (item.points.length === 0) {
      return;
    }
    ctx.strokeStyle = item.color;
    ctx.lineWidth = 2.5;
    ctx.beginPath();

    item.points.forEach((point, index) => {
      const x = chartBox.left + ((point.x - xMin) / xRange) * usableWidth;
      const y = chartBox.bottom - ((point.y - yMin) / yRange) * usableHeight;
      if (index === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    });

    ctx.stroke();
  });
}

async function loadFile(path) {
  const text = await fetchText(path);
  const parsed = parseLog(text);

  state.selectedFile = path;
  state.delimiter = parsed.delimiter;
  state.headers = parsed.headers;
  state.rows = parsed.rows;

  $("current-file").textContent = path.split("/").pop() || path;
  renderFileList(state.fileEntries);
  renderTable();
  drawChart();
  setMessage(`Loaded ${state.selectedFile}`, "info");
}

async function refreshAll(preferredFile = "") {
  state.directory = $("folder-path").value.trim() || "/logs/csv";

  const status = await fetchJson("/api/status");
  updateStatus(status);

  const entries = await fetchJson(`/api/list?dir=${encodeURIComponent(state.directory)}`);
  state.fileEntries = entries;
  renderFileList(entries);

  const target =
    preferredFile ||
    state.selectedFile ||
    status.activeLog ||
    (entries.length > 0 ? entries[0].path : "");

  if (target && entries.some((entry) => entry.path === target)) {
    await loadFile(target);
  } else if (entries.length > 0) {
    await loadFile(entries[0].path);
  } else {
    state.selectedFile = "";
    state.headers = [];
    state.rows = [];
    $("current-file").textContent = "No file selected";
    renderTable();
    drawChart();
    setMessage("No log files found yet.", "info");
  }
}

function downloadCurrent() {
  if (!state.selectedFile) {
    setMessage("Choose a log file first.", "warn");
    return;
  }

  const blob = new Blob([collectTableText()], { type: "text/plain;charset=utf-8" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = state.selectedFile.split("/").pop() || "log.txt";
  document.body.appendChild(link);
  link.click();
  URL.revokeObjectURL(link.href);
  link.remove();
}

async function saveCurrent() {
  if (!state.selectedFile) {
    setMessage("Choose a log file first.", "warn");
    return;
  }

  await fetchJson(`/api/file?path=${encodeURIComponent(state.selectedFile)}`, {
    method: "PUT",
    headers: { "Content-Type": "text/plain;charset=utf-8" },
    body: collectTableText(),
  });

  setMessage(`Saved ${state.selectedFile.split("/").pop()}`, "info");
  await refreshAll(state.selectedFile);
}

async function deleteCurrent() {
  if (!state.selectedFile) {
    setMessage("Choose a log file first.", "warn");
    return;
  }

  const confirmed = window.confirm(`Delete ${state.selectedFile.split("/").pop()}?`);
  if (!confirmed) {
    return;
  }

  await fetchJson(`/api/file?path=${encodeURIComponent(state.selectedFile)}`, {
    method: "DELETE",
  });

  setMessage("Log file deleted.", "info");
  await refreshAll();
}

async function deleteAll() {
  const confirmed = window.confirm("Delete every log file in the current folder?");
  if (!confirmed) {
    return;
  }

  await fetchJson(`/api/logs?dir=${encodeURIComponent(state.directory)}`, {
    method: "DELETE",
  });

  setMessage("All archived logs deleted. A new active log has been created.", "info");
  await refreshAll();
}

function bindEvents() {
  $("refresh-btn").addEventListener("click", async () => {
    try {
      await refreshAll();
    } catch (error) {
      setMessage(error.message || "Refresh failed.", "warn");
    }
  });

  $("download-btn").addEventListener("click", downloadCurrent);
  $("save-btn").addEventListener("click", async () => {
    try {
      await saveCurrent();
    } catch (error) {
      setMessage(error.message || "Save failed.", "warn");
    }
  });

  $("delete-btn").addEventListener("click", async () => {
    try {
      await deleteCurrent();
    } catch (error) {
      setMessage(error.message || "Delete failed.", "warn");
    }
  });

  $("delete-all-btn").addEventListener("click", async () => {
    try {
      await deleteAll();
    } catch (error) {
      setMessage(error.message || "Delete all failed.", "warn");
    }
  });

  window.addEventListener("resize", drawChart);
}

window.addEventListener("DOMContentLoaded", async () => {
  bindEvents();
  try {
    await refreshAll();
  } catch (error) {
    setMessage(error.message || "The UI failed to load.", "warn");
  }
});
