/* =========================================================
   TOAST
========================================================= */

export let toastTimer;

export function showToast(message){

    const toast =
    document.getElementById(
        "toast"
    );

    toast.innerText =
    message;

    toast.classList.add(
        "show"
    );


    clearTimeout(
        toastTimer
    );


    toastTimer =
    setTimeout(
        () => {

            toast.classList.remove(
                "show"
            );

        },
        1800
    );

};
