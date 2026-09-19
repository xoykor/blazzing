(function (global) {
  "use strict";

  function render(element, text) {
    var qr;

    if (!element) {
      return false;
    }

    element.innerHTML = "";

    if (typeof global.qrcode !== "function") {
      element.textContent = "QR indisponível";
      return false;
    }

    try {
      qr = global.qrcode(0, "M");
      qr.addData(text);
      qr.make();
      element.innerHTML = qr.createSvgTag({
        cellSize: 5,
        margin: 4,
        scalable: true
      });
      return true;
    } catch (error) {
      element.textContent = "QR indisponível";
      return false;
    }
  }

  global.BlazzingQR = {
    render: render
  };
}(window));
