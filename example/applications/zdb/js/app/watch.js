export function watch(store, select, render) {
    let previous;
    let ready = false;
    return store.subscribe((state) => {
        const next = select(state);
        if (ready && same(previous, next)) return;
        ready = true;
        previous = next;
        render(state);
    });
}

function same(left, right) {
    if (Object.is(left, right)) return true;
    if (typeof left !== "object" || typeof right !== "object" || !left || !right) return false;
    const keys = Object.keys(left);
    if (keys.length !== Object.keys(right).length) return false;
    for (const key of keys) {
        if (!Object.prototype.hasOwnProperty.call(right, key) || !Object.is(left[key], right[key])) return false;
    }
    return true;
}
