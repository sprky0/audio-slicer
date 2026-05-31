/**
 * Persistence for jsloop.
 *
 * Settings (small JSON) live in localStorage; audio files (large Blobs) live in
 * IndexedDB, which has no practical size cap. Both layers degrade gracefully —
 * if storage is unavailable (private mode, quota), calls fail quietly and the app
 * just runs without persistence rather than throwing.
 */

const STATE_KEY = 'jsloop.state.v1';
const DB_NAME   = 'jsloop';
const STORE     = 'audio';
const DB_VERSION= 1;

// ---------------------------------------------------------------------------
// State (localStorage)
// ---------------------------------------------------------------------------

export function saveStateRaw(obj) {
	try {
		localStorage.setItem(STATE_KEY, JSON.stringify(obj));
	} catch (err) {
		console.warn('Failed to save state:', err);
	}
}

export function loadStateRaw() {
	try {
		const raw = localStorage.getItem(STATE_KEY);
		return raw ? JSON.parse(raw) : null;
	} catch (err) {
		console.warn('Failed to load state:', err);
		return null;
	}
}

export function clearStateRaw() {
	try {
		localStorage.removeItem(STATE_KEY);
	} catch (err) {
		console.warn('Failed to clear state:', err);
	}
}

// ---------------------------------------------------------------------------
// Audio (IndexedDB)
// ---------------------------------------------------------------------------

function openDB() {
	return new Promise((resolve, reject) => {
		const req = indexedDB.open(DB_NAME, DB_VERSION);
		req.onupgradeneeded = () => {
			if (!req.result.objectStoreNames.contains(STORE)) {
				req.result.createObjectStore(STORE);
			}
		};
		req.onsuccess = () => resolve(req.result);
		req.onerror   = () => reject(req.error);
	});
}

// Run a single transaction against the audio store and resolve with the request
// result. `fn(store)` returns the IDBRequest to await.
async function tx(mode, fn) {
	const db = await openDB();
	return new Promise((resolve, reject) => {
		const transaction = db.transaction(STORE, mode);
		const store       = transaction.objectStore(STORE);
		let request;
		try {
			request = fn(store);
		} catch (err) {
			reject(err);
			return;
		}
		transaction.oncomplete = () => resolve(request ? request.result : undefined);
		transaction.onerror    = () => reject(transaction.error);
		transaction.onabort    = () => reject(transaction.error);
	});
}

export async function putAudio(key, blob) {
	try {
		await tx('readwrite', (store) => store.put(blob, key));
	} catch (err) {
		console.warn('Failed to store audio:', err);
	}
}

export async function getAudio(key) {
	try {
		return await tx('readonly', (store) => store.get(key));
	} catch (err) {
		console.warn('Failed to read audio:', err);
		return undefined;
	}
}

export async function deleteAudio(key) {
	try {
		await tx('readwrite', (store) => store.delete(key));
	} catch (err) {
		console.warn('Failed to delete audio:', err);
	}
}

export async function clearAudio() {
	try {
		await tx('readwrite', (store) => store.clear());
	} catch (err) {
		console.warn('Failed to clear audio:', err);
	}
}
