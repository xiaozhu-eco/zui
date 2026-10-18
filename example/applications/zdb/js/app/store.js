export function createStore(initial) {
    let state = initial;
    const listeners = new Set();
    return {
        get() {
            return state;
        },
        update(recipe) {
            const next = { ...state };
            recipe(next);
            next.rev = state.rev + 1;
            state = next;
            for (const listener of [...listeners]) listener(state);
        },
        subscribe(listener) {
            listeners.add(listener);
            listener(state);
            return () => listeners.delete(listener);
        },
    };
}
