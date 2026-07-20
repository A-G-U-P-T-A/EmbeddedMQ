(() => {
  const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (reduce) return;

  const panels = document.querySelectorAll(".panel");
  const io = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("in-view");
          io.unobserve(entry.target);
        }
      });
    },
    { threshold: 0.16 }
  );

  panels.forEach((panel) => {
    panel.style.opacity = "0";
    panel.style.transform = "translateY(18px)";
    panel.style.transition = "opacity 0.7s ease, transform 0.7s ease";
    io.observe(panel);
  });

  const style = document.createElement("style");
  style.textContent = `.panel.in-view{opacity:1!important;transform:none!important}`;
  document.head.appendChild(style);
})();
