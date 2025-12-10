import { createRoot } from "react-dom/client";
import { App } from "./App";

window.addEventListener("load", () => {
  let root = createRoot(document.getElementById("root"));
  root.render(<App />);
});
