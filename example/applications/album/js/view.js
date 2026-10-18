import {
  addFiles,
  loadLibrary,
  removeAlbum,
  removePhoto,
  restoreLibrary,
  saveAlbum,
  savePhoto,
  uid,
} from "./db.js";

const MIN_ZOOM = 1;
const MAX_ZOOM = 4;
const ZOOM_STEP = 0.5;

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

export function mount() {
  const nativePicks = document.getElementById("album-picks");
  if (nativePicks) nativePicks.hidden = true;
  const status = document.getElementById("status");
  const albumsEl = document.getElementById("albums");
  const titleEl = document.getElementById("album-title");
  const nameInput = document.getElementById("album-name");
  const countEl = document.getElementById("album-count");
  const deleteAlbumBtn = document.getElementById("delete-album");
  const gridEl = document.getElementById("grid");
  const emptyEl = document.getElementById("empty");
  const lightbox = document.getElementById("lightbox");
  const stage = document.getElementById("stage");
  const img = document.getElementById("light-img");
  const noteInput = document.getElementById("light-note");
  const albumSelect = document.getElementById("light-album");
  const inspector = document.getElementById("light-inspector");
  const confirmBack = document.getElementById("confirm");
  const confirmBody = document.getElementById("confirm-body");
  const albumDialog = document.getElementById("album-dialog");
  const albumForm = document.getElementById("album-form");
  const albumFormName = document.getElementById("album-form-name");
  const albumFormError = document.getElementById("album-form-error");
  const fileInput = document.getElementById("file");

  let albums = [];
  let photos = [];
  let current = "all";
  let lightId = null;
  let zoom = 1;
  let panned = false;
  let ready = false;
  const urls = new Map();
  let chain = Promise.resolve();

  function enqueue(work) {
    const run = chain.then(() => work());
    chain = run.then(() => {}, () => {});
    return run;
  }

  function showError(error) {
    console.error(error);
    status.classList.add("is-error");
    status.textContent = "没能写入本地相册";
  }

  function urlFor(photo) {
    let url = urls.get(photo.id);
    if (!url) {
      url = URL.createObjectURL(photo.blob);
      urls.set(photo.id, url);
    }
    return url;
  }

  function revoke(id) {
    const url = urls.get(id);
    if (!url) return;
    URL.revokeObjectURL(url);
    urls.delete(id);
  }

  function revokeAll() {
    for (const url of urls.values()) URL.revokeObjectURL(url);
    urls.clear();
  }

  function visiblePhotos() {
    const query = (document.getElementById("photo-search")?.value || "").trim().toLowerCase();
    const inAlbum = current === "all" ? photos : photos.filter((photo) => photo.albumId === current);
    if (!query) return inAlbum;
    return inAlbum.filter((photo) => {
      const name = (photo.name || "").toLowerCase();
      const note = (photo.note || "").toLowerCase();
      return name.includes(query) || note.includes(query);
    });
  }

  function albumById(id) {
    return albums.find((album) => album.id === id) || null;
  }

  function confirmBox(body, okLabel) {
    confirmBody.textContent = body;
    document.getElementById("confirm-ok").textContent = okLabel || "删除";
    confirmBack.hidden = false;
    document.getElementById("confirm-ok").focus();
    return new Promise((resolve) => {
      const finish = (value) => {
        confirmBack.hidden = true;
        resolve(value);
      };
      document.getElementById("confirm-ok").onclick = () => finish(true);
      document.getElementById("confirm-cancel").onclick = () => finish(false);
    });
  }

  async function refresh() {
    const data = await loadLibrary();
    albums = data.albums;
    photos = data.photos;
  }

  function snapshotNote() {
    return { id: lightId, note: noteInput.value.trim() };
  }

  async function persistSnapshot(snap) {
    if (!snap.id) return;
    const photo = photos.find((item) => item.id === snap.id);
    if (!photo || photo.note === snap.note) return;
    photo.note = snap.note;
    await savePhoto(photo);
  }

  function render() {
    const list = visiblePhotos();
    if (lightId && !photos.some((photo) => photo.id === lightId)) lightId = null;
    renderSidebar();
    renderGallery(list);
    renderLight(list);
  }

  function renderSidebar() {
    const items = [{ id: "all", name: "全部" }, ...albums];
    albumsEl.replaceChildren();
    for (const item of items) {
      const button = el("button", "album-btn");
      button.type = "button";
      button.dataset.id = item.id;
      if (item.id === current) button.setAttribute("aria-current", "true");
      const count = item.id === "all"
        ? photos.length
        : photos.filter((photo) => photo.albumId === item.id).length;
      button.append(el("span", "album-label", item.name), el("span", "album-count", String(count)));
      button.addEventListener("click", () => {
        if (current === item.id) return;
        const snap = snapshotNote();
        enqueue(() => persistSnapshot(snap)).then(() => {
          current = item.id;
          lightId = null;
          zoom = 1;
          render();
        }).catch(showError);
      });
      albumsEl.append(button);
    }
  }

  function renderGallery(list) {
    const album = albumById(current);
    const editable = Boolean(album) && album.id !== "loose";
    titleEl.hidden = editable;
    nameInput.hidden = !editable;
    if (current === "all") titleEl.textContent = "全部";
    else if (!editable) titleEl.textContent = album ? album.name : "";
    if (editable && document.activeElement !== nameInput) nameInput.value = album.name;
    countEl.textContent = `${list.length} 张`;
    deleteAlbumBtn.hidden = !album || album.id === "loose";
    emptyEl.hidden = list.length > 0;
    gridEl.replaceChildren();
    for (const photo of list) {
      const tile = el("button", "tile");
      tile.type = "button";
      const image = el("img");
      image.src = urlFor(photo);
      image.alt = photo.name;
      image.draggable = false;
      const copy = el("span", "tile-copy");
      copy.append(el("span", "tile-name", photo.name));
      if (photo.note) copy.append(el("span", "tile-note", photo.note));
      tile.append(image, copy);
      tile.addEventListener("click", () => openPhoto(photo.id));
      gridEl.append(tile);
    }
  }

  function renderLight(list) {
    if (!lightId) {
      lightbox.hidden = true;
      stage.classList.remove("is-zoomed");
      return;
    }
    const photo = photos.find((item) => item.id === lightId);
    if (!photo) {
      lightbox.hidden = true;
      return;
    }
    lightbox.hidden = false;
    const index = list.findIndex((item) => item.id === photo.id);
    document.getElementById("light-name").textContent = photo.name;
    document.getElementById("light-pos").textContent = index >= 0 ? `${index + 1} / ${list.length}` : "";
    document.getElementById("light-prev").disabled = index <= 0;
    document.getElementById("light-next").disabled = index < 0 || index >= list.length - 1;
    const switched = img.dataset.photoId !== photo.id;
    if (switched) {
      img.dataset.photoId = photo.id;
      img.alt = photo.name;
      img.src = urlFor(photo);
      noteInput.value = photo.note;
    } else if (document.activeElement !== noteInput) {
      noteInput.value = photo.note;
    }
    const selected = photo.albumId;
    albumSelect.replaceChildren();
    for (const album of albums) {
      const option = el("option", "", album.name);
      option.value = album.id;
      albumSelect.append(option);
    }
    albumSelect.value = selected;
    const strip = document.getElementById("filmstrip");
    if (strip) {
      strip.replaceChildren();
      for (const item of list) {
        const frame = el("button", item.id === photo.id ? "film is-on" : "film");
        frame.type = "button";
        frame.title = item.name;
        const thumb = el("img");
        thumb.src = urlFor(item);
        thumb.alt = item.name;
        thumb.draggable = false;
        frame.append(thumb);
        frame.addEventListener("click", () => openPhoto(item.id));
        strip.append(frame);
      }
    }
    setZoom(zoom);
    panned = false;
    requestAnimationFrame(applyFrame);
  }

  function applyFrame() {
    const nw = img.naturalWidth;
    const nh = img.naturalHeight;
    if (!nw || !nh || !stage.clientWidth || !stage.clientHeight) return;
    const scale = Math.min(stage.clientWidth / nw, stage.clientHeight / nh);
    img.style.width = `${Math.max(1, Math.round(nw * scale * zoom))}px`;
    img.style.height = `${Math.max(1, Math.round(nh * scale * zoom))}px`;
  }

  function setZoom(next) {
    zoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, next));
    document.getElementById("zoom-label").textContent = `${Math.round(zoom * 100)}%`;
    document.getElementById("zoom-out").disabled = zoom <= MIN_ZOOM;
    document.getElementById("zoom-in").disabled = zoom >= MAX_ZOOM;
    stage.classList.toggle("is-zoomed", zoom > MIN_ZOOM);
    applyFrame();
  }

  function openPhoto(id) {
    if (id !== lightId) zoom = 1;
    lightId = id;
    render();
    requestAnimationFrame(applyFrame);
  }

  async function step(delta) {
    const list = visiblePhotos();
    const index = list.findIndex((photo) => photo.id === lightId);
    const next = list[index + delta];
    if (!next) return;
    const snap = snapshotNote();
    await enqueue(() => persistSnapshot(snap));
    if (lightId !== snap.id) return;
    lightId = next.id;
    zoom = 1;
    render();
  }

  async function closeLight() {
    const snap = snapshotNote();
    await enqueue(() => persistSnapshot(snap));
    if (lightId !== snap.id) return;
    lightId = null;
    zoom = 1;
    img.dataset.photoId = "";
    render();
  }

  img.addEventListener("load", applyFrame);
  window.addEventListener("resize", () => {
    if (lightId) applyFrame();
  });

  stage.addEventListener("wheel", (event) => {
    if (lightbox.hidden) return;
    event.preventDefault();
    setZoom(zoom + (event.deltaY < 0 ? ZOOM_STEP : -ZOOM_STEP));
  }, { passive: false });

  stage.addEventListener("pointerdown", (event) => {
    panned = false;
    if (zoom <= MIN_ZOOM || event.button !== 0) return;
    const pointerId = event.pointerId;
    const originX = event.clientX;
    const originY = event.clientY;
    const startLeft = stage.scrollLeft;
    const startTop = stage.scrollTop;
    const move = (ev) => {
      if (ev.pointerId !== pointerId) return;
      if (Math.abs(ev.clientX - originX) > 3 || Math.abs(ev.clientY - originY) > 3) panned = true;
      stage.scrollLeft = startLeft - (ev.clientX - originX);
      stage.scrollTop = startTop - (ev.clientY - originY);
    };
    const up = (ev) => {
      if (ev.pointerId !== pointerId) return;
      stage.removeEventListener("pointermove", move);
      stage.removeEventListener("pointerup", up);
      stage.removeEventListener("pointercancel", up);
    };
    stage.addEventListener("pointermove", move);
    stage.addEventListener("pointerup", up);
    stage.addEventListener("pointercancel", up);
    try {
      stage.setPointerCapture(pointerId);
    } catch {
      // Untrusted pointers cannot be captured; the move listener still pans.
    }
  });

  // 信息 opens the tools - the bar, the filmstrip and the caption/album form - over the photo.
  // The checkbox is the toggle in the browser; native flips it from the `i` key.
  inspector.addEventListener("change", () => requestAnimationFrame(applyFrame));
  document.getElementById("light-close").addEventListener("click", () => {
    closeLight().catch(showError);
  });
  document.getElementById("light-prev").addEventListener("click", () => {
    step(-1).catch(showError);
  });
  document.getElementById("light-next").addEventListener("click", () => {
    step(1).catch(showError);
  });
  document.getElementById("photo-search").addEventListener("input", () => render());
  document.getElementById("zoom-in").addEventListener("click", () => setZoom(zoom + ZOOM_STEP));
  document.getElementById("zoom-out").addEventListener("click", () => setZoom(zoom - ZOOM_STEP));

  // With the tools out of the way, the photo is the whole page: a click on it, on the backdrop or
  // beside a smaller photo closes the viewer. A pan that moved is not a click, and while the
  // tools are up their clicks belong to them.
  lightbox.addEventListener("click", (event) => {
    if (panned || inspector.checked) return;
    if (event.target instanceof Element && event.target.closest(".nav-btn")) return;
    closeLight().catch(showError);
  });

  noteInput.addEventListener("blur", () => {
    const snap = snapshotNote();
    enqueue(() => persistSnapshot(snap)).catch(showError);
  });

  document.getElementById("light-form").addEventListener("submit", (event) => {
    event.preventDefault();
    const snap = snapshotNote();
    enqueue(() => persistSnapshot(snap)).catch(showError);
  });

  albumSelect.addEventListener("change", () => {
    const snap = snapshotNote();
    const albumId = albumSelect.value;
    enqueue(async () => {
      await persistSnapshot(snap);
      const photo = photos.find((item) => item.id === snap.id);
      if (!photo || photo.albumId === albumId) return;
      photo.albumId = albumId;
      await savePhoto(photo);
      await refresh();
    }).then(() => {
      if (lightId && !visiblePhotos().some((photo) => photo.id === lightId)) {
        lightId = null;
        zoom = 1;
      }
      render();
    }).catch(showError);
  });

  document.getElementById("light-delete").addEventListener("click", async () => {
    const photo = photos.find((item) => item.id === lightId);
    if (!photo) return;
    const snap = snapshotNote();
    const list = visiblePhotos();
    const index = list.findIndex((item) => item.id === photo.id);
    const ok = await confirmBox(`删除「${photo.name}」？`);
    if (!ok || lightId !== photo.id) {
      enqueue(() => persistSnapshot(snap)).catch(showError);
      return;
    }
    try {
      await enqueue(async () => {
        await persistSnapshot(snap);
        await removePhoto(photo.id);
        await refresh();
      });
      revoke(photo.id);
      const rest = visiblePhotos();
      lightId = rest.length ? rest[Math.min(index, rest.length - 1)].id : null;
      if (!lightId) zoom = 1;
      render();
    } catch (error) {
      showError(error);
    }
  });

  nameInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      nameInput.blur();
    }
  });
  nameInput.addEventListener("blur", () => {
    const album = albumById(current);
    if (!album || album.id === "loose") return;
    const next = nameInput.value.trim();
    if (!next || next === album.name) {
      nameInput.value = album.name;
      return;
    }
    enqueue(async () => {
      album.name = next;
      await saveAlbum(album);
      await refresh();
    }).then(render).catch(showError);
  });

  deleteAlbumBtn.addEventListener("click", async () => {
    const album = albumById(current);
    if (!album || album.id === "loose") return;
    const count = photos.filter((photo) => photo.albumId === album.id).length;
    const extra = count ? `里面 ${count} 张照片会挪到未分类。` : "这个相册是空的。";
    const ok = await confirmBox(`删除「${album.name}」？${extra}`);
    if (!ok) return;
    try {
      await enqueue(async () => {
        await removeAlbum(album.id);
        await refresh();
      });
      if (current === album.id) current = "all";
      render();
    } catch (error) {
      showError(error);
    }
  });

  document.getElementById("add-album").addEventListener("click", () => {
    albumFormError.textContent = "";
    albumFormName.value = "";
    albumDialog.hidden = false;
    albumFormName.focus();
  });
  document.getElementById("album-form-cancel").addEventListener("click", () => {
    albumDialog.hidden = true;
  });
  albumForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const name = albumFormName.value.trim();
    if (!name) {
      albumFormError.textContent = "请填写名称";
      return;
    }
    const album = { id: uid(), name };
    enqueue(async () => {
      await saveAlbum(album);
      await refresh();
    }).then(() => {
      albumDialog.hidden = true;
      current = album.id;
      lightId = null;
      zoom = 1;
      render();
      nameInput.focus();
      nameInput.select();
    }).catch(showError);
  });

  fileInput.addEventListener("change", () => {
    const files = [...fileInput.files];
    fileInput.value = "";
    if (!ready || !files.length) return;
    const albumId = current === "all" ? "loose" : current;
    const albumName = current === "all" ? "未分类" : (albumById(current)?.name || "未分类");
    enqueue(async () => {
      const result = await addFiles(files, albumId);
      await refresh();
      return result;
    }).then((result) => {
      const parts = [];
      if (result.accepted.length) parts.push(`已导入 ${result.accepted.length} 张到「${albumName}」`);
      if (result.skipped.length) parts.push(result.skipped.join("，"));
      status.classList.toggle("is-error", !result.accepted.length && result.skipped.length > 0);
      status.textContent = parts.join("。");
      render();
    }).catch(showError);
  });

  document.getElementById("restore").addEventListener("click", async () => {
    const ok = await confirmBox("用示例相册替换当前内容？导入的照片会消失。", "恢复");
    if (!ok) return;
    try {
      status.textContent = "正在准备相册…";
      status.classList.remove("is-error");
      await enqueue(async () => {
        const data = await restoreLibrary();
        albums = data.albums;
        photos = data.photos;
      });
      revokeAll();
      current = "all";
      lightId = null;
      zoom = 1;
      img.dataset.photoId = "";
      status.textContent = "";
      render();
    } catch (error) {
      showError(error);
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      if (!confirmBack.hidden) {
        document.getElementById("confirm-cancel").click();
        return;
      }
      if (!albumDialog.hidden) {
        albumDialog.hidden = true;
        return;
      }
      if (!lightbox.hidden) closeLight().catch(showError);
      return;
    }
    if (lightbox.hidden || !confirmBack.hidden || !albumDialog.hidden) return;
    if (event.altKey || event.ctrlKey || event.metaKey || event.isComposing) return;
    if (event.target instanceof Element && event.target.closest("input, textarea, select")) return;
    if (event.key === "ArrowLeft") {
      event.preventDefault();
      step(-1).catch(showError);
    } else if (event.key === "ArrowRight") {
      event.preventDefault();
      step(1).catch(showError);
    } else if (event.key === "Delete" || event.key === "Backspace") {
      event.preventDefault();
      document.getElementById("light-delete").click();
    } else if (event.key === "i" || event.key === "I") {
      event.preventDefault();
      inspector.checked = !inspector.checked;
      requestAnimationFrame(applyFrame);
    } else if (event.key === "+" || event.key === "=") {
      event.preventDefault();
      setZoom(zoom + ZOOM_STEP);
    } else if (event.key === "-" || event.key === "_") {
      event.preventDefault();
      setZoom(zoom - ZOOM_STEP);
    }
  });

  status.textContent = "正在准备相册…";
  refresh().then(() => {
    ready = true;
    status.textContent = "";
    status.classList.remove("is-error");
    render();
  }).catch((error) => {
    console.error(error);
    status.classList.add("is-error");
    status.textContent = "相册没有打开";
  });
}
